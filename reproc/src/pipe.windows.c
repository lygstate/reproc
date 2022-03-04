#ifndef _WIN32_WINNT
  #define _WIN32_WINNT 0x0600 // _WIN32_WINNT_VISTA
#elif _WIN32_WINNT < 0x0600
  #error "_WIN32_WINNT must be greater than _WIN32_WINNT_VISTA (0x0600)"
#endif

#include "pipe.h"

#include <limits.h>
#include <stdlib.h>
#include <windows.h>
#include <winsock2.h>

#include "error.h"
#include "handle.h"
#include "macro.h"

const SOCKET PIPE_INVALID = INVALID_SOCKET;

const short PIPE_EVENT_IN = POLLIN;
const short PIPE_EVENT_OUT = POLLOUT;
#pragma comment(lib, "advapi32.lib")
#define RtlGenRandom SystemFunction036
DECLSPEC_IMPORT BOOLEAN WINAPI RtlGenRandom(
  PVOID RandomBuffer,
  ULONG RandomBufferLength
);

// Inspired by https://gist.github.com/geertj/4325783.
static int socketpair(int domain, int type, int protocol, SOCKET *out)
{
  SOCKET server = PIPE_INVALID;
  SOCKET pair[] = { PIPE_INVALID, PIPE_INVALID };
  int r = -1;
  union {
    struct sockaddr_in inaddr;
    SOCKADDR_STORAGE addr;
  } name = { 0 };
  int size = sizeof(name);
  SOCKADDR_IN localhost = { 0 };
  struct {
    WSAPROTOCOL_INFOW data;
    int size;
  } info = { { 0 }, sizeof(WSAPROTOCOL_INFOW) };
  int reuse = 1;
  uint16_t loopback_increment = rand() & 0xFFFF;
  uint32_t loopback_address = INADDR_LOOPBACK + (1 << 16);
  RtlGenRandom(&loopback_increment, sizeof(loopback_increment));
  // Add randome value for loopback address for
  // avoid address/port conflict
  // TODO: try multiple times
  loopback_address += loopback_increment;
  ASSERT(out);

  server = WSASocketW(AF_INET, SOCK_STREAM, 0, NULL, 0, 0);
  if (server == INVALID_SOCKET) {
    goto error;
  }

  /* set SO_REUSEADDR option to avoid leaking some windows resource.
   * Windows System Error 10049, "Event ID 4226 TCP/IP has reached
   * the security limit imposed on the number of concurrent TCP connect
   * attempts."  Bleah.
   */
  if (setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (char *) &reuse,
                 (socklen_t) sizeof(reuse)) < 0) {
    goto error;
  }
  localhost.sin_family = AF_INET;
  localhost.sin_addr.S_un.S_addr = htonl(loopback_address);
  localhost.sin_port = 0;

  r = bind(server, (SOCKADDR *) &localhost, sizeof(localhost));
  if (r < 0) {
    goto error;
  }

  r = listen(server, 1);
  if (r < 0) {
    goto error;
  }

  r = getsockname(server, (SOCKADDR *) &name, &size);
  if (r < 0) {
    goto error;
  }

  // win32 getsockname may only set the port number, p=0.0005.
  // ( http://msdn.microsoft.com/library/ms738543.aspx ):
  name.inaddr.sin_addr.s_addr = htonl(loopback_address);
  name.inaddr.sin_family = AF_INET;

  pair[0] = WSASocketW(domain, type, protocol, NULL, 0, 0);
  if (pair[0] == INVALID_SOCKET) {
    goto error;
  }

  r = getsockopt(pair[0], SOL_SOCKET, SO_PROTOCOL_INFOW, (char *) &info.data,
                 &info.size);
  if (r < 0) {
    goto error;
  }

  // We require the returned sockets to be usable as Windows file handles. This
  // might not be the case if extra LSP providers are installed.

  if (!(info.data.dwServiceFlags1 & XP1_IFS_HANDLES)) {
    r = -ERROR_NOT_SUPPORTED;
    goto finish;
  }

  r = pipe_nonblocking(pair[0], true);
  if (r < 0) {
    goto finish;
  }

  /* TODO: how to make sure it's connected to the `server` just listen ? */
  r = connect(pair[0], (SOCKADDR *) &name, size);
  if (r < 0 && WSAGetLastError() != WSAEWOULDBLOCK) {
    goto error;
  }

  r = pipe_nonblocking(pair[0], false);
  if (r < 0) {
    goto finish;
  }

  pair[1] = accept(server, NULL, NULL);
  if (pair[1] == INVALID_SOCKET) {
    goto error;
  }

  out[0] = pair[0];
  out[1] = pair[1];

  pair[0] = PIPE_INVALID;
  pair[1] = PIPE_INVALID;

error:
  r = -WSAGetLastError();
finish:
  pipe_destroy(server);
  pipe_destroy(pair[0]);
  pipe_destroy(pair[1]);

  return r;
}

int pipe_init(SOCKET *read, SOCKET *write)
{
  SOCKET pair[] = { PIPE_INVALID, PIPE_INVALID };
  int r = -1;

  ASSERT(read);
  ASSERT(write);

  // Use sockets instead of pipes so we can use `WSAPoll` which only works with
  // sockets.
  r = socketpair(AF_INET, SOCK_STREAM, 0, pair);
  if (r < 0) {
    goto finish;
  }

  r = SetHandleInformation((HANDLE) pair[0], HANDLE_FLAG_INHERIT, 0);
  if (r == 0) {
    r = -(int) GetLastError();
    goto finish;
  }

  r = SetHandleInformation((HANDLE) pair[1], HANDLE_FLAG_INHERIT, 0);
  if (r == 0) {
    r = -(int) GetLastError();
    goto finish;
  }

  // Make the connection unidirectional to better emulate a pipe.

  r = shutdown(pair[0], SD_SEND);
  if (r < 0) {
    r = -WSAGetLastError();
    goto finish;
  }

  r = shutdown(pair[1], SD_RECEIVE);
  if (r < 0) {
    r = -WSAGetLastError();
    goto finish;
  }

  *read = pair[0];
  *write = pair[1];

  pair[0] = PIPE_INVALID;
  pair[1] = PIPE_INVALID;

finish:
  pipe_destroy(pair[0]);
  pipe_destroy(pair[1]);

  return r;
}

int pipe_nonblocking(SOCKET pipe, bool enable)
{
  u_long mode = enable;
  int r = ioctlsocket(pipe, (long) FIONBIO, &mode);
  return r < 0 ? -WSAGetLastError() : 0;
}

int pipe_read(SOCKET pipe, uint8_t *buffer, size_t size)
{
  int r = 0;

  ASSERT(pipe != PIPE_INVALID);
  ASSERT(buffer);
  ASSERT(size <= INT_MAX);

  r = recv(pipe, (char *) buffer, (int) size, 0);

  if (r == 0) {
    return -ERROR_BROKEN_PIPE;
  }

  return r < 0 ? -WSAGetLastError() : r;
}

int pipe_write(SOCKET pipe, const uint8_t *buffer, size_t size)
{
  int r = 0;

  ASSERT(pipe != PIPE_INVALID);
  ASSERT(buffer);
  ASSERT(size <= INT_MAX);

  r = send(pipe, (const char *) buffer, (int) size, 0);

  return r < 0 ? -WSAGetLastError() : r;
}

int pipe_poll(pipe_event_source *sources, size_t num_sources, int timeout)
{
  WSAPOLLFD *pollfds = NULL;
  int r = -1;
  size_t i = 0;

  ASSERT(num_sources <= INT_MAX);

  pollfds = calloc(num_sources, sizeof(WSAPOLLFD));
  if (pollfds == NULL) {
    r = -ERROR_NOT_ENOUGH_MEMORY;
    goto finish;
  }

  for (i = 0; i < num_sources; i++) {
    pollfds[i].fd = sources[i].pipe;
    pollfds[i].events = sources[i].interests;
  }

  r = WSAPoll(pollfds, (ULONG) num_sources, timeout);
  if (r < 0) {
    r = -WSAGetLastError();
    goto finish;
  }

  for (i = 0; i < num_sources; i++) {
    sources[i].events = pollfds[i].revents;
  }

finish:
  free(pollfds);

  return r;
}

int pipe_shutdown(SOCKET pipe)
{
  int r = 0;

  if (pipe == PIPE_INVALID) {
    return 0;
  }

  r = shutdown(pipe, SD_SEND);
  return r < 0 ? -WSAGetLastError() : 0;
}

SOCKET pipe_destroy(SOCKET pipe)
{
  int r = 0;

  if (pipe == PIPE_INVALID) {
    return PIPE_INVALID;
  }

  r = closesocket(pipe);
  ASSERT_UNUSED(r == 0);

  return PIPE_INVALID;
}

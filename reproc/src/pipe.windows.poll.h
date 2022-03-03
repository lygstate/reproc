#pragma once

#include "pipe.h"

#include <reproc/util.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS

#include <windows.h>
#include <winioctl.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <ntstatus.h>
#include <winternl.h>

#include <mswsock.h>

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 4200)
#endif
typedef struct _AFD_POLL_HANDLE_INFO {
  HANDLE Handle;
  ULONG PollEvents;
  NTSTATUS Status;
} AFD_POLL_HANDLE_INFO, *PAFD_POLL_HANDLE_INFO;

typedef struct _AFD_POLL_INFO {
  LARGE_INTEGER Timeout;
  ULONG NumberOfHandles;
  BOOLEAN Unique;
  AFD_POLL_HANDLE_INFO Handles[1];
} AFD_POLL_INFO, *PAFD_POLL_INFO;

// Taken directly from ReactOS sources
/* AFD event bits */
#define AFD_POLL_RECEIVE_BIT 0 // 0001
#define AFD_POLL_RECEIVE (1 << AFD_POLL_RECEIVE_BIT)
#define AFD_POLL_RECEIVE_EXPEDITED_BIT 1 // 0002
#define AFD_POLL_RECEIVE_EXPEDITED (1 << AFD_POLL_RECEIVE_EXPEDITED_BIT)
#define AFD_POLL_SEND_BIT 2 // 0004
#define AFD_POLL_SEND (1 << AFD_POLL_SEND_BIT)
#define AFD_POLL_DISCONNECT_BIT 3 // 0008
#define AFD_POLL_DISCONNECT (1 << AFD_POLL_DISCONNECT_BIT)
#define AFD_POLL_ABORT_BIT 4 // 0010
#define AFD_POLL_ABORT (1 << AFD_POLL_ABORT_BIT)
#define AFD_POLL_LOCAL_CLOSE_BIT 5 // 0020
#define AFD_POLL_LOCAL_CLOSE (1 << AFD_POLL_LOCAL_CLOSE_BIT)
#define AFD_POLL_CONNECT_BIT 6 // 0040
#define AFD_POLL_CONNECT (1 << AFD_POLL_CONNECT_BIT)
#define AFD_POLL_ACCEPT_BIT 7 // 0080
#define AFD_POLL_ACCEPT (1 << AFD_POLL_ACCEPT_BIT)
#define AFD_POLL_CONNECT_FAIL_BIT 8 // 0100
#define AFD_POLL_CONNECT_FAIL (1 << AFD_POLL_CONNECT_FAIL_BIT)
#define AFD_POLL_QOS_BIT 9 // 0200
#define AFD_POLL_QOS (1 << AFD_POLL_QOS_BIT)
#define AFD_POLL_GROUP_QOS_BIT 10 // 0400
#define AFD_POLL_GROUP_QOS (1 << AFD_POLL_GROUP_QOS_BIT)

#define AFD_POLL_ROUTING_IF_CHANGE_BIT 11 // 0800
#define AFD_POLL_ROUTING_IF_CHANGE (1 << AFD_POLL_ROUTING_IF_CHANGE_BIT)
#define AFD_POLL_ADDRESS_LIST_CHANGE_BIT 12 // 1000
#define AFD_POLL_ADDRESS_LIST_CHANGE (1 << AFD_POLL_ADDRESS_LIST_CHANGE_BIT)
#define AFD_NUM_POLL_EVENTS 13
#define AFD_POLL_ALL ((1 << AFD_NUM_POLL_EVENTS) - 1)

#define AFD_POLL_SANCOUNTS_UPDATED 0x80000000

/* IOCTL Generation */
#define FSCTL_AFD_BASE FILE_DEVICE_NETWORK
#define _AFD_CONTROL_CODE(request, method)                                     \
  ((FSCTL_AFD_BASE) << 12 | (request << 2) | method)

/* AFD Commands */
#define AFD_POLL 9

#define IOCTL_AFD_POLL _AFD_CONTROL_CODE(AFD_POLL, METHOD_BUFFERED)
#define RTL_CONSTANT_STRING(s)                                                 \
  {                                                                            \
    sizeof(s) - sizeof((s)[0]), sizeof(s), s                                   \
  }

#if defined(__MINGW32__) && defined(_WIN64)
  #define RTL_CONSTANT_OBJECT_ATTRIBUTES(ObjectName, Attributes)               \
    {                                                                          \
      sizeof(OBJECT_ATTRIBUTES), 0UL, NULL, ObjectName, Attributes, 0UL, NULL, \
          NULL                                                                 \
    }
#else
  #define RTL_CONSTANT_OBJECT_ATTRIBUTES(ObjectName, Attributes)               \
    {                                                                          \
      sizeof(OBJECT_ATTRIBUTES), NULL, ObjectName, Attributes, NULL, NULL      \
    }
#endif

static UNICODE_STRING afd__device_name = RTL_CONSTANT_STRING(
    L"\\Device\\Afd\\wsapoll");

static OBJECT_ATTRIBUTES afd__device_attributes =
    RTL_CONSTANT_OBJECT_ATTRIBUTES(&afd__device_name, 0);

static int with_socket_error(DWORD error)
{
  WSASetLastError((int) error);
  return SOCKET_ERROR;
}

static BOOL wait_for_status(HANDLE event)
{
  // Initial 0.5 seconds wait
  DWORD status = WaitForSingleObjectEx(event, 500, TRUE);
  if (status == WAIT_OBJECT_0)
    return TRUE;

  do {
    status = WaitForSingleObjectEx(event, INFINITE, TRUE);
  } while (status == STATUS_USER_APC);

  return status == STATUS_WAIT_0 ? TRUE : FALSE;
}

typedef NTSTATUS(NTAPI *NtDeviceIoControlFileFunction)(
    IN HANDLE FileHandle,
    IN HANDLE Event OPTIONAL,
    IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
    IN PVOID ApcContext OPTIONAL,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN ULONG IoControlCode,
    IN PVOID InputBuffer OPTIONAL,
    IN ULONG InputBufferLength,
    OUT PVOID OutputBuffer OPTIONAL,
    IN ULONG OutputBufferLength);

typedef ULONG(NTAPI *RtlNtStatusToDosErrorFunction)(NTSTATUS Status);
typedef NTSTATUS(NTAPI *NtCreateFileFunction)(
    OUT PHANDLE FileHandle,
    IN ACCESS_MASK DesiredAccess,
    IN POBJECT_ATTRIBUTES ObjectAttributes,
    OUT PIO_STATUS_BLOCK IoStatusBlock,
    IN PLARGE_INTEGER AllocationSize OPTIONAL,
    IN ULONG FileAttributes,
    IN ULONG ShareAccess,
    IN ULONG CreateDisposition,
    IN ULONG CreateOptions,
    IN PVOID EaBuffer OPTIONAL,
    IN ULONG EaLength);

NtDeviceIoControlFileFunction pNtDeviceIoControlFile = NULL;
RtlNtStatusToDosErrorFunction pRtlNtStatusToDosError = NULL;
NtCreateFileFunction pNtCreateFile = NULL;

static HANDLE device_handle = NULL;

static void init_once(void)
{
  HANDLE ntdll = GetModuleHandleW(L"ntdll.dll");
  pNtDeviceIoControlFile = (NtDeviceIoControlFileFunction) (uintptr_t)
      GetProcAddress(ntdll, "NtDeviceIoControlFile");
  pRtlNtStatusToDosError = (RtlNtStatusToDosErrorFunction) (uintptr_t)
      GetProcAddress(ntdll, "RtlNtStatusToDosError");
  pNtCreateFile = (NtCreateFileFunction) (uintptr_t)
      GetProcAddress(ntdll, "NtCreateFile");

  if (pNtCreateFile != NULL) {
    IO_STATUS_BLOCK iosb;
    pNtCreateFile(&device_handle, SYNCHRONIZE, &afd__device_attributes, &iosb,
                  NULL, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN, 0,
                  NULL, 0);
  }
}

static uintptr_t init_once_flag = REPROC_ONCE_FLAG_INIT; // NOLINT

static void wpInit()
{
  reproc_call_once(&init_once_flag, init_once);
}

static NTSTATUS NTAPI
wpNtDeviceIoControlFile(IN HANDLE FileHandle,
                        IN HANDLE Event OPTIONAL,
                        IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
                        IN PVOID ApcContext OPTIONAL,
                        OUT PIO_STATUS_BLOCK IoStatusBlock,
                        IN ULONG IoControlCode,
                        IN PVOID InputBuffer OPTIONAL,
                        IN ULONG InputBufferLength,
                        OUT PVOID OutputBuffer OPTIONAL,
                        IN ULONG OutputBufferLength)
{
  wpInit();

  if (pNtDeviceIoControlFile != NULL) {
    return pNtDeviceIoControlFile(FileHandle, Event, ApcRoutine, ApcContext,
                                  IoStatusBlock, IoControlCode, InputBuffer,
                                  InputBufferLength, OutputBuffer,
                                  OutputBufferLength);
  }
  return STATUS_NOT_SUPPORTED;
}

static ULONG NTAPI wpRtlNtStatusToDosErrorFunction(NTSTATUS Status)
{
  wpInit();

  if (pNtDeviceIoControlFile != NULL) {
    return pRtlNtStatusToDosError(Status);
  }
  return WSAEOPNOTSUPP;
}

static NTSTATUS NTAPI wpNtCreateFile(OUT PHANDLE FileHandle,
                                     IN ACCESS_MASK DesiredAccess,
                                     IN POBJECT_ATTRIBUTES ObjectAttributes,
                                     OUT PIO_STATUS_BLOCK IoStatusBlock,
                                     IN PLARGE_INTEGER AllocationSize OPTIONAL,
                                     IN ULONG FileAttributes,
                                     IN ULONG ShareAccess,
                                     IN ULONG CreateDisposition,
                                     IN ULONG CreateOptions,
                                     IN PVOID EaBuffer OPTIONAL,
                                     IN ULONG EaLength)
{
  wpInit();

  if (pNtDeviceIoControlFile != NULL) {
    return pNtCreateFile(FileHandle, DesiredAccess, ObjectAttributes,
                         IoStatusBlock, AllocationSize, FileAttributes,
                         ShareAccess, CreateDisposition, CreateOptions,
                         EaBuffer, EaLength);
  }
  return STATUS_NOT_SUPPORTED;
}

struct handle_index_pair {
  HANDLE handle;
  ULONG index;
};

static int compare_poll_handle(void const *a, void const *b)
{
  return memcmp(a, b, sizeof(HANDLE));
}

static int poll_win32(struct pollfd *fdArray, ULONG fds, INT timeout)
{
  enum { invalid_event_mask = POLLPRI | POLLWRBAND };
  ULONG i = 0U;
  ULONG mem_size = 0;
  AFD_POLL_INFO *poll_info = NULL;
  ULONG NumberOfHandles = 0;
  NTSTATUS status = STATUS_UNSUCCESSFUL;
  int poll_ret = 0;
  struct handle_index_pair *handle_map;
  if (fdArray == NULL || fds == 0)
    return with_socket_error(WSAEINVAL);
  // Count real number of sockets for which status is requested
  for (i = 0U; i < fds; ++i) {
    if (fdArray[i].fd != INVALID_SOCKET) {
      if (fdArray[i].events & invalid_event_mask)
        return with_socket_error(WSAEINVAL);
      ++NumberOfHandles;
    }
  }

  // No real socket was given
  if (NumberOfHandles == 0)
    return with_socket_error(WSAENOTSOCK);

  handle_map = HeapAlloc(GetProcessHeap(), 0,
                         NumberOfHandles * sizeof(struct handle_index_pair));
  if (handle_map == NULL) {
    return with_socket_error(WSA_NOT_ENOUGH_MEMORY);
  }

  // Allocate (only if necessary) for poll info buffer
  mem_size = (ULONG) (sizeof(AFD_POLL_INFO) +
                      (NumberOfHandles - 1) * sizeof(AFD_POLL_HANDLE_INFO));

  // Prepare poll info
  poll_info = (AFD_POLL_INFO *) HeapAlloc(GetProcessHeap(), 0, mem_size);
  if (poll_info == NULL) {
    HeapFree(GetProcessHeap(), 0, handle_map);
    return with_socket_error(WSA_NOT_ENOUGH_MEMORY);
  }
  poll_info->Timeout.QuadPart = timeout < 0 ? INT64_MAX
                                            : timeout * (-10 * 1000LL);
  poll_info->Unique = FALSE;

  NumberOfHandles = 0;
  for (i = 0U; i < fds; ++i) {
    AFD_POLL_HANDLE_INFO *handle_info = NULL;
    DWORD bytes = 0;
    SOCKET base_socket = INVALID_SOCKET;
    SOCKET socket = fdArray[i].fd;
    if (socket == INVALID_SOCKET)
      continue;

    handle_info = poll_info->Handles + NumberOfHandles;

    /* clang-format off */
    /* Even though Microsoft documentation clearly states that LSPs should
     * never intercept the `SIO_BASE_HANDLE` ioctl [1], Komodia based LSPs do
     * so anyway, breaking it, with the apparent intention of preventing LSP
     * bypass [2]. Fortunately they don't handle `SIO_BSP_HANDLE_POLL`, which
     * will at least let us obtain the socket associated with the next winsock
     * protocol chain entry. If this succeeds, loop around and call
     * `SIO_BASE_HANDLE` again with the returned BSP socket, to make sure that
     * we unwrap all layers and retrieve the actual base socket.
     *  [1] https://docs.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls
     *  [2] https://www.komodia.com/newwiki/index.php?title=Komodia%27s_Redirector_bug_fixes#Version_2.2.2.6
     */
    /* clang-format on */
    /* TODO: mapping the handles */
    if (WSAIoctl(socket, SIO_BASE_HANDLE, NULL, 0U, &base_socket,
                 sizeof(base_socket), &bytes, NULL, NULL) != SOCKET_ERROR) {
      handle_info->Handle = (HANDLE) base_socket;
    } else if (WSAIoctl(socket, SIO_BSP_HANDLE_POLL, NULL, 0U, &base_socket,
                        sizeof(base_socket), &bytes, NULL,
                        NULL) != SOCKET_ERROR) {
      handle_info->Handle = (HANDLE) base_socket;
    } else {
      handle_info->Handle = (HANDLE) socket;
    }

    handle_map[NumberOfHandles].handle = handle_info->Handle;
    handle_map[NumberOfHandles].index = i;

    handle_info->Status = 0;

    // Error events are always received which is is different than what we'd got
    // with select() call
    handle_info->PollEvents = AFD_POLL_ABORT | AFD_POLL_DISCONNECT |
                              AFD_POLL_CONNECT_FAIL;

    if (fdArray[i].events & POLLRDNORM) {
      handle_info->PollEvents |= AFD_POLL_ACCEPT | AFD_POLL_RECEIVE;
    }
    if (fdArray[i].events & POLLRDBAND)
      handle_info->PollEvents |= AFD_POLL_RECEIVE_EXPEDITED;
    if (fdArray[i].events & POLLWRNORM)
      handle_info->PollEvents |= AFD_POLL_SEND | AFD_POLL_CONNECT;
    ++NumberOfHandles;
  }
  poll_info->NumberOfHandles = NumberOfHandles;
  if (poll_info->NumberOfHandles > 0) {
    IO_STATUS_BLOCK iosb;
    HANDLE poll_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (poll_event != NULL) {
      wpInit();
      status = wpNtDeviceIoControlFile(device_handle, poll_event, NULL, NULL,
                                       &iosb, IOCTL_AFD_POLL, poll_info,
                                       mem_size, poll_info, mem_size);
      // poll() is blocking, so we must wait for the event to be set
      // if timeout was something else than 0
      if (status == STATUS_PENDING) {
        BOOL res = wait_for_status(poll_event);
        if (res) {
          /* Wait succeed */
          status = iosb.Status;
        } else {
          poll_ret = with_socket_error(GetLastError());
        }
      }
      CloseHandle(poll_event);
    } else {
      poll_ret = with_socket_error(GetLastError());
    }
  }
  if (status == STATUS_TIMEOUT) {
    status = STATUS_SUCCESS;
  }
  if (poll_ret == 0 && status == STATUS_SUCCESS) {
    ULONG j = 0;
    for (i = 0U; i < fds; ++i) {
      if (fdArray[i].fd == INVALID_SOCKET) {
        fdArray[i].revents = POLLNVAL;
      }
    }
    qsort(handle_map, NumberOfHandles, sizeof(handle_map[0]),
          compare_poll_handle);
    for (j = 0U; j < poll_info->NumberOfHandles; ++j) {
      const ULONG POLLWRNORM_MASK = (AFD_POLL_CONNECT_FAIL | AFD_POLL_SEND |
                                     AFD_POLL_CONNECT);
      AFD_POLL_HANDLE_INFO *handle_info = poll_info->Handles + j;
      struct handle_index_pair *found_item = bsearch(&handle_info->Handle,
                                                     handle_map,
                                                     NumberOfHandles,
                                                     sizeof(handle_map[0]),
                                                     compare_poll_handle);
      if (found_item == NULL) {
        continue;
      }
      i = found_item->index;
      if ((i >= fds) || (fdArray[i].fd == INVALID_SOCKET)) {
        continue;
      }
      fdArray[i].revents = 0;
      handle_info = bsearch(&fdArray[i].fd, poll_info->Handles,
                            poll_info->NumberOfHandles,
                            sizeof(poll_info->Handles[0]), compare_poll_handle);
      if (handle_info == NULL) {
        continue;
      }

      if ((handle_info->PollEvents & (AFD_POLL_ACCEPT | AFD_POLL_RECEIVE)) &&
          (fdArray[i].events & POLLRDNORM)) {
        fdArray[i].revents |= POLLRDNORM;
      }

      if ((handle_info->PollEvents & AFD_POLL_RECEIVE_EXPEDITED) &&
          (fdArray[i].events & POLLRDBAND)) {
        fdArray[i].revents |= POLLRDBAND;
      }

      //++win10fix
      if ((handle_info->PollEvents & POLLWRNORM_MASK) &&
          (fdArray[i].events & POLLWRNORM)) {
        fdArray[i].revents |= POLLWRNORM; // POLLOUT
      }

      if (handle_info->PollEvents & AFD_POLL_DISCONNECT) {
        fdArray[i].revents |= POLLHUP;
      }

      //++win10fix
      if (handle_info->PollEvents & (AFD_POLL_CONNECT_FAIL | AFD_POLL_ABORT)) {
        fdArray[i].revents |= POLLHUP | POLLERR;
      }
      if ((handle_info->PollEvents & AFD_POLL_LOCAL_CLOSE) != 0) {
        fdArray[i].revents |= POLLNVAL;
      }
    }
  }

  if (poll_ret == 0 && status != STATUS_SUCCESS) {
    poll_ret = with_socket_error(wpRtlNtStatusToDosErrorFunction(status));
  }
  if (poll_ret == 0) {
    poll_ret = (int) poll_info->NumberOfHandles;
  }
  HeapFree(GetProcessHeap(), 0, handle_map);
  HeapFree(GetProcessHeap(), 0, poll_info);
  return poll_ret;
}

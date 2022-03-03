#include <reproc/drain.h>

#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "macro.h"

static int
reproc_drain_single(reproc_t *process, reproc_sink sink, REPROC_STREAM stream)
{
  uint8_t buffer[4096];
  size_t bytes_read = 0;
  int r = reproc_read(process, stream, buffer, ARRAY_SIZE(buffer));
  if (r < 0 && r != REPROC_EPIPE) {
    return r;
  }

  bytes_read = r == REPROC_EPIPE ? 0 : (size_t) r;

  return sink.function(stream, buffer, bytes_read, sink.context);
}

int reproc_drain(reproc_t *process, reproc_sink out, reproc_sink err)
{
  const uint8_t initial = 0;
  int r = -1;

  ASSERT_EINVAL(process);
  ASSERT_EINVAL(out.function);
  ASSERT_EINVAL(err.function);

  // A single call to `read` might contain multiple messages. By always calling
  // both sinks once with no data before reading, we give them the chance to
  // process all previous output one by one before reading from the child
  // process again.

  r = out.function(REPROC_STREAM_IN, &initial, 0, out.context);
  if (r != 0) {
    return r;
  }

  r = err.function(REPROC_STREAM_IN, &initial, 0, err.context);
  if (r != 0) {
    return r;
  }

  for (;;) {
    int r_out = 0;
    int r_err = 0;
    reproc_event_source source = { process, REPROC_EVENT_OUT | REPROC_EVENT_ERR,
                                   0 };
    r = reproc_poll(&source, 1, REPROC_INFINITE);
    if (r < 0) {
      r = r == REPROC_EPIPE ? 0 : r;
      break;
    }

    if (source.events & REPROC_EVENT_DEADLINE) {
      r = REPROC_ETIMEDOUT;
      break;
    }

    if (source.events & (REPROC_EVENT_OUT | REPROC_EVENT_EXIT)) {
      r_out = reproc_drain_single(process, out, REPROC_STREAM_OUT);
    }
    if (source.events & (REPROC_EVENT_ERR | REPROC_EVENT_EXIT)) {
      r_err = reproc_drain_single(process, err, REPROC_STREAM_ERR);
    }
    r = 0;
    if (r_out != 0 || r_err != 0) {
      r = r_out != 0 ? r_out : r_err;
    }
    if (r != 0) {
      break;
    }
    if ((source.events & REPROC_EVENT_EXIT) &&
        !(source.events & (REPROC_EVENT_OUT | REPROC_EVENT_ERR))) {
      /* Only the exit event leave behind */
      break;
    }
  }

  return r;
}

static int sink_string(REPROC_STREAM stream,
                       const uint8_t *buffer,
                       size_t size,
                       void *context)
{
  char **string = (char **) context;
  size_t string_size = *string == NULL ? 0 : strlen(*string);

  char *r = (char *) realloc(*string, string_size + size + 1);

  (void) stream;

  if (r == NULL) {
    return REPROC_ENOMEM;
  }

  *string = r;
  memcpy(*string + string_size, buffer, size);
  (*string)[string_size + size] = '\0';

  return 0;
}

reproc_sink reproc_sink_string(char **output)
{
  reproc_sink sink = { sink_string, output };
  return sink;
}

static int sink_discard(REPROC_STREAM stream,
                        const uint8_t *buffer,
                        size_t size,
                        void *context)
{
  (void) stream;
  (void) buffer;
  (void) size;
  (void) context;

  return 0;
}

reproc_sink reproc_sink_discard(void)
{
  reproc_sink sink = { sink_discard, NULL };
  return sink;
}

const reproc_sink REPROC_SINK_NULL = { sink_discard, NULL };

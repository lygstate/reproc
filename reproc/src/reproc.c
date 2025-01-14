#include <reproc/reproc.h>

#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "error.h"
#include "handle.h"
#include "init.h"
#include "macro.h"
#include "options.h"
#include "process.h"
#include "redirect.h"

enum {
  STATUS_NOT_STARTED = -1,
  STATUS_IN_PROGRESS = -2,
  STATUS_IN_CHILD = -3,
};

#define SIGOFFSET 128

const int REPROC_SIGKILL = SIGOFFSET + 9;
const int REPROC_SIGTERM = SIGOFFSET + 15;

const int REPROC_INFINITE = -1;
const int REPROC_DEADLINE = -2;

static int setup_input(pipe_type *pipe, const uint8_t *data, size_t size)
{
  // `reproc_write` only needs the child process stdin pipe to be initialized.
  size_t written = 0;
  int r = -1;

  if (data == NULL) {
    ASSERT(size == 0);
    return 0;
  }

  ASSERT(pipe && *pipe != PIPE_INVALID);

  // Make sure we don't block indefinitely when `input` is bigger than the
  // size of the pipe.
  r = pipe_nonblocking(*pipe, true);
  if (r < 0) {
    return r;
  }

  while (written < size) {
    r = pipe_write(*pipe, data + written, size - written);
    if (r < 0) {
      return r;
    }

    ASSERT(written + (size_t) r <= size);
    written += (size_t) r;
  }

  *pipe = pipe_destroy(*pipe);

  return 0;
}

static int expiry(int timeout, int64_t deadline)
{
  int64_t n = 0;
  int remaining = 0;

  if (timeout == REPROC_INFINITE && deadline == REPROC_INFINITE) {
    return REPROC_INFINITE;
  }

  if (deadline == REPROC_INFINITE) {
    return timeout;
  }

  n = now();

  if (n >= deadline) {
    return REPROC_DEADLINE;
  }

  // `deadline` exceeds `now` by at most a full `int` so the cast is safe.
  remaining = (int) (deadline - n);

  if (timeout == REPROC_INFINITE) {
    return remaining;
  }

  return MIN(timeout, remaining);
}

static size_t find_earliest_deadline(reproc_event_source *sources,
                                     size_t num_sources)
{
  size_t earliest = 0;
  int min = REPROC_INFINITE;
  size_t i = 0;

  ASSERT(sources);
  ASSERT(num_sources > 0);

  for (i = 0; i < num_sources; i++) {
    reproc_t *process = sources[i].process;
    int current = 0;

    if (process == NULL) {
      continue;
    }

    current = expiry(REPROC_INFINITE, process->deadline);

    if (current == REPROC_DEADLINE) {
      return i;
    }

    if (min == REPROC_INFINITE || current < min) {
      earliest = i;
      min = current;
    }
  }

  return earliest;
}

reproc_t *reproc_new(void)
{
  reproc_t *process = malloc(sizeof(reproc_t));
  if (process == NULL) {
    return NULL;
  }

  memset(process, 0, sizeof(process[0]));
  process->handle = PROCESS_INVALID;
  process->pipe.in = PIPE_INVALID;
  process->pipe.out = PIPE_INVALID;
  process->pipe.err = PIPE_INVALID;
  process->pipe.exit = PIPE_INVALID;
  process->child.out = PIPE_INVALID;
  process->child.err = PIPE_INVALID;
  process->status = STATUS_NOT_STARTED;
  process->deadline = REPROC_INFINITE;

  return process;
}

int reproc_start(reproc_t *process,
                 const char *const *argv,
                 reproc_options options)
{
  struct {
    handle_type in;
    handle_type out;
    handle_type err;
    pipe_type exit;
  } child = { HANDLE_INVALID, HANDLE_INVALID, HANDLE_INVALID, PIPE_INVALID };
  int r = -1;
  struct process_options process_options = { 0 };

  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status == STATUS_NOT_STARTED);

  r = init();
  if (r < 0) {
    return r; // Make sure we can always call `deinit` in `finish`.
  }

  r = parse_options(&options, argv);
  if (r < 0) {
    goto finish;
  }

  r = redirect_init(&process->pipe.in, &child.in, REPROC_STREAM_IN,
                    options.redirect.in, options.nonblocking, HANDLE_INVALID);
  if (r < 0) {
    goto finish;
  }

  r = redirect_init(&process->pipe.out, &child.out, REPROC_STREAM_OUT,
                    options.redirect.out, options.nonblocking, HANDLE_INVALID);
  if (r < 0) {
    goto finish;
  }

  r = redirect_init(&process->pipe.err, &child.err, REPROC_STREAM_ERR,
                    options.redirect.err, options.nonblocking, child.out);
  if (r < 0) {
    goto finish;
  }

  r = pipe_init(&process->pipe.exit, &child.exit);
  if (r < 0) {
    goto finish;
  }

  r = setup_input(&process->pipe.in, options.input.data, options.input.size);
  if (r < 0) {
    goto finish;
  }

  process_options.env.behavior = options.env.behavior;
  process_options.env.extra = options.env.extra;
  process_options.working_directory = options.working_directory,
  process_options.show_console_window = options.show_console_window;
  process_options.handle.in = child.in;
  process_options.handle.out = child.out,
  process_options.handle.err = child.err,
  process_options.handle.exit = (handle_type) child.exit;

  r = process_start(process, argv, process_options);
  if (r < 0) {
    goto finish;
  }

  if (r > 0) {
    process->stop = options.stop;

    if (options.deadline != REPROC_INFINITE) {
      process->deadline = now() + options.deadline;
    }

    process->nonblocking = options.nonblocking;
  }

finish:
  // Either an error has ocurred or the child pipe endpoints have been copied to
  // the stdin/stdout/stderr streams of the child process. Either way, they can
  // be safely closed.
  redirect_destroy(child.in, options.redirect.in.type);

  // See `reproc_poll` for why we do this.

#ifdef _WIN32
  if (r < 0 || options.redirect.out.type != REPROC_REDIRECT_PIPE) {
    child.out = redirect_destroy(child.out, options.redirect.out.type);
  }

  if (r < 0 || options.redirect.err.type != REPROC_REDIRECT_PIPE) {
    child.err = redirect_destroy(child.err, options.redirect.err.type);
  }
#else
  child.out = redirect_destroy(child.out, options.redirect.out.type);
  child.err = redirect_destroy(child.err, options.redirect.err.type);
#endif

  pipe_destroy(child.exit);

  if (r < 0) {
    process->handle = process_destroy(process->handle);
    process->pipe.in = pipe_destroy(process->pipe.in);
    process->pipe.out = pipe_destroy(process->pipe.out);
    process->pipe.err = pipe_destroy(process->pipe.err);
    process->pipe.exit = pipe_destroy(process->pipe.exit);
    deinit();
  } else if (r == 0) {
    process->handle = PROCESS_INVALID;
    // `process_start` has already taken care of closing the handles for us.
    process->pipe.in = PIPE_INVALID;
    process->pipe.out = PIPE_INVALID;
    process->pipe.err = PIPE_INVALID;
    process->pipe.exit = PIPE_INVALID;
    process->status = STATUS_IN_CHILD;
  } else {
    process->child.out = (pipe_type) child.out;
    process->child.err = (pipe_type) child.err;
    process->status = STATUS_IN_PROGRESS;
  }

  return r;
}

enum { PIPES_PER_SOURCE = 4 };

static bool contains_valid_pipe(pipe_event_source *sources, size_t num_sources)
{
  size_t i = 0;
  for (i = 0; i < num_sources; i++) {
    if (sources[i].pipe != PIPE_INVALID) {
      return true;
    }
  }

  return false;
}

int reproc_poll(reproc_event_source *sources, size_t num_sources, int timeout)
{
  size_t earliest = 0;
  int64_t deadline = 0;
  size_t i = 0;
  int first = 0;
  size_t num_pipes = 0;
  int r = REPROC_ENOMEM;
  pipe_event_source *pipes = NULL;

  ASSERT_EINVAL(sources);
  ASSERT_EINVAL(num_sources > 0);

  earliest = find_earliest_deadline(sources, num_sources);
  deadline = sources[earliest].process == NULL
                         ? REPROC_INFINITE
                         : sources[earliest].process->deadline;

  first = expiry(timeout, deadline);
  num_pipes = num_sources * PIPES_PER_SOURCE;

  if (first == REPROC_DEADLINE) {
    for (i = 0; i < num_sources; i++) {
      sources[i].events = 0;
    }

    sources[earliest].events = REPROC_EVENT_DEADLINE;
    return 1;
  }

  pipes = calloc(num_pipes, sizeof(pipe_event_source));
  if (pipes == NULL) {
    return r;
  }

  for (i = 0; i < num_pipes; i++) {
    pipes[i].pipe = PIPE_INVALID;
  }

  for (i = 0; i < num_sources; i++) {
    size_t j = i * PIPES_PER_SOURCE;
    reproc_t *process = sources[i].process;
    int interests = sources[i].interests;
    bool in = false;
    bool out = false;
    bool err = false;
    bool exit = false;

    if (process == NULL) {
      continue;
    }

    in = interests & REPROC_EVENT_IN;
    pipes[j + 0].pipe = in ? process->pipe.in : PIPE_INVALID;
    pipes[j + 0].interests = PIPE_EVENT_OUT;

    out = interests & REPROC_EVENT_OUT;
    pipes[j + 1].pipe = out ? process->pipe.out : PIPE_INVALID;
    pipes[j + 1].interests = PIPE_EVENT_IN;

    err = interests & REPROC_EVENT_ERR;
    pipes[j + 2].pipe = err ? process->pipe.err : PIPE_INVALID;
    pipes[j + 2].interests = PIPE_EVENT_IN;

    exit = (interests & REPROC_EVENT_EXIT) ||
                (interests & REPROC_EVENT_OUT &&
                 process->child.out != PIPE_INVALID) ||
                (interests & REPROC_EVENT_ERR &&
                 process->child.err != PIPE_INVALID);
    pipes[j + 3].pipe = exit ? process->pipe.exit : PIPE_INVALID;
    pipes[j + 3].interests = PIPE_EVENT_IN;
  }

  if (!contains_valid_pipe(pipes, num_pipes)) {
    r = REPROC_EPIPE;
    goto finish;
  }

  r = pipe_poll(pipes, num_pipes, first);
  if (r < 0) {
    goto finish;
  }

  for (i = 0; i < num_sources; i++) {
    sources[i].events = 0;
  }

  if (r == 0 && first != timeout) {
    // Differentiate between timeout and deadline expiry. Deadline expiry is an
    // event, timeouts are not.
    sources[earliest].events = REPROC_EVENT_DEADLINE;
    r = 1;
  } else if (r > 0) {
    // Convert pipe events to process events.
    for (i = 0; i < num_pipes; i++) {
      if (pipes[i].pipe == PIPE_INVALID) {
        continue;
      }

      if (pipes[i].events > 0) {
        // Index in a set of pipes determines the process pipe and thus the
        // process event.
        // 0 = stdin pipe => REPROC_EVENT_IN
        // 1 = stdout pipe => REPROC_EVENT_OUT
        // ...
        int event = 1 << (i % PIPES_PER_SOURCE);
        sources[i / PIPES_PER_SOURCE].events |= event;
      }
    }

    r = 0;

    // Count the number of processes with events.
    for (i = 0; i < num_sources; i++) {
      r += sources[i].events > 0;
    }
  }

finish:
  free(pipes);

  return r;
}

int reproc_read(reproc_t *process,
                REPROC_STREAM stream,
                uint8_t *buffer,
                size_t size)
{
  pipe_type *pipe = NULL;
  pipe_type *child = NULL;
  int r = -1;

  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);
  ASSERT_EINVAL(stream == REPROC_STREAM_OUT || stream == REPROC_STREAM_ERR);
  ASSERT_EINVAL(buffer);

  pipe = stream == REPROC_STREAM_OUT ? &process->pipe.out
                                                : &process->pipe.err;
  child = stream == REPROC_STREAM_OUT ? &process->child.out
                                                : &process->child.err;

  if (*pipe == PIPE_INVALID) {
    return REPROC_EPIPE;
  }

  // If we've kept extra handles open in the parent, make sure we use
  // `reproc_poll` which closes the extra handles we keep open when the child
  // process exits. If we don't, `pipe_read` will block forever because the
  // extra handles we keep open in the parent would never be closed.
  if (*child != PIPE_INVALID) {
    // On Windows, when redirecting to sockets, we keep the child handles alive
    // in the parent process (see `reproc_start`). We do this because Windows
    // doesn't correctly flush redirected socket handles when a child process
    // exits. This can lead to data loss where the parent process doesn't
    // receive all output of the child process. To get around this, we keep an
    // extra handle open in the parent process which we close correctly when we
    // detect the child process has exited. Detecting whether a child process
    // has exited happens via another inherited socket, but here there's no
    // danger of data loss because no data is received over this socket.
    int interests = stream == REPROC_STREAM_OUT ? REPROC_EVENT_OUT
                                            : REPROC_EVENT_ERR;
    reproc_event_source source = { process, interests, 0 };
    r = reproc_poll(&source, 1, process->nonblocking ? 0 : REPROC_INFINITE);
    if (source.events & REPROC_EVENT_EXIT) {
      pipe_shutdown(*child);
      *child = pipe_destroy(*child);
    }
    if (r <= 0) {
      return r == 0 ? REPROC_EWOULDBLOCK : r;
    }
  }

  r = pipe_read(*pipe, buffer, size);

  if (r == REPROC_EPIPE) {
    *pipe = pipe_destroy(*pipe);
  }

  return r;
}

int reproc_write(reproc_t *process, const uint8_t *buffer, size_t size)
{
  int r = REPROC_EPIPE;

  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);

  if (buffer == NULL) {
    // Allow `NULL` buffers but only if `size == 0`.
    ASSERT_EINVAL(size == 0);
    return 0;
  }

  if (process->pipe.in == PIPE_INVALID) {
    return REPROC_EPIPE;
  }

  r = pipe_write(process->pipe.in, buffer, size);

  if (r == REPROC_EPIPE) {
    process->pipe.in = pipe_destroy(process->pipe.in);
  }

  return r;
}

int reproc_close(reproc_t *process, REPROC_STREAM stream)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);

  switch (stream) {
    case REPROC_STREAM_IN:
      process->pipe.in = pipe_destroy(process->pipe.in);
      return 0;
    case REPROC_STREAM_OUT:
      process->pipe.out = pipe_destroy(process->pipe.out);
      return 0;
    case REPROC_STREAM_ERR:
      process->pipe.err = pipe_destroy(process->pipe.err);
      return 0;
  }

  return REPROC_EINVAL;
}

int reproc_wait(reproc_t *process, int timeout)
{
  int r = -1;
  pipe_event_source source = { 0 };

  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);
  ASSERT_EINVAL(process->status != STATUS_NOT_STARTED);

  if (process->status >= 0) {
    return process->status;
  }

  if (timeout == REPROC_DEADLINE) {
    timeout = expiry(REPROC_INFINITE, process->deadline);
    // If the deadline has expired, `expiry` returns `REPROC_DEADLINE` which
    // means we'll only check if the process is still running.
    if (timeout == REPROC_DEADLINE) {
      timeout = 0;
    }
  }

  ASSERT(process->pipe.exit != PIPE_INVALID);

  source.pipe = process->pipe.exit;
  source.interests = PIPE_EVENT_IN;

  r = pipe_poll(&source, 1, timeout);
  if (r <= 0) {
    return r == 0 ? REPROC_ETIMEDOUT : r;
  }

  r = process_wait(process->handle, timeout);
  if (r < 0) {
    return r;
  }

  process->pipe.exit = pipe_destroy(process->pipe.exit);

  return process->status = r;
}

int reproc_terminate(reproc_t *process)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);
  ASSERT_EINVAL(process->status != STATUS_NOT_STARTED);

  if (process->status >= 0) {
    return 0;
  }

  return process_terminate(process);
}

int reproc_kill(reproc_t *process)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);
  ASSERT_EINVAL(process->status != STATUS_NOT_STARTED);

  if (process->status >= 0) {
    return 0;
  }

  return process_kill(process->handle);
}

int reproc_stop(reproc_t *process, reproc_stop_actions stop)
{
  reproc_stop_action actions[3] = { 0 };
  int r = -1;
  size_t i = 0;

  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);
  ASSERT_EINVAL(process->status != STATUS_NOT_STARTED);

  stop = parse_stop_actions(stop);

  actions[0] = stop.first;
  actions[1] = stop.second;
  actions[2] = stop.third;
  for (i = 0; i < ARRAY_SIZE(actions); i++) {
    r = REPROC_EINVAL; // NOLINT

    switch (actions[i].action) {
      case REPROC_STOP_NOOP:
        r = 0;
        continue;
      case REPROC_STOP_WAIT:
        r = 0;
        break;
      case REPROC_STOP_TERMINATE:
        r = reproc_terminate(process);
        break;
      case REPROC_STOP_KILL:
        r = reproc_kill(process);
        break;
    }

    // Stop if `reproc_terminate` or `reproc_kill` fail.
    if (r < 0) {
      break;
    }

    r = reproc_wait(process, actions[i].timeout);
    if (r != REPROC_ETIMEDOUT) {
      break;
    }
  }

  return r;
}

int reproc_pid(reproc_t *process)
{
  ASSERT_EINVAL(process);
  ASSERT_EINVAL(process->status != STATUS_IN_CHILD);
  ASSERT_EINVAL(process->status != STATUS_NOT_STARTED);

  return process_pid(process->handle);
}

reproc_t *reproc_destroy(reproc_t *process)
{
  ASSERT_RETURN(process, NULL);

  if (process->status == STATUS_IN_PROGRESS) {
    reproc_stop_actions stop;
    stop.first.action = REPROC_STOP_WAIT;
    stop.first.timeout = 100;
    stop.second.action = REPROC_STOP_TERMINATE;
    stop.second.timeout = 100;
    stop.third.action = REPROC_STOP_KILL;
    stop.third.timeout = 100;
    reproc_stop(process, stop);
  }

  process_destroy(process->handle);
  if (process->extra != NULL) {
    free(process->extra);
  }
  pipe_destroy(process->pipe.in);
  pipe_destroy(process->pipe.out);
  pipe_destroy(process->pipe.err);
  pipe_destroy(process->pipe.exit);

  pipe_destroy(process->child.out);
  pipe_destroy(process->child.err);

  if (process->status != STATUS_NOT_STARTED) {
    deinit();
  }

  free(process);

  return NULL;
}

const char *reproc_strerror(int error)
{
  return error_string(error);
}

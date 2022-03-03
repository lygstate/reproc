#ifdef _WIN32
  #include <windows.h>
static void millisleep(long ms)
{
  Sleep((DWORD) ms);
}
static int getpid()
{
  return (int) GetCurrentProcessId();
}
#else
  #define _POSIX_C_SOURCE 200809L
  #include <time.h>
  #include <unistd.h>
static inline void millisleep(long ms)
{
  nanosleep(&(struct timespec){ .tv_sec = (ms) / 1000,
                                .tv_nsec = ((ms) % 1000L) * 1000000 },
            NULL);
}
#endif

#include <stdlib.h>
#include <string.h>

#include <reproc/reproc.h>

enum { NUM_CHILDREN = 20 };

static int reproc_pipe_read(reproc_event_source *source,
                            uint8_t *buffer,
                            size_t size,
                            int event_to_read,
                            int *event_state)
{
  int r = 0;
  if ((source->events & (event_to_read | REPROC_EVENT_EXIT)) &&
      (source->interests & event_to_read)) {
    REPROC_STREAM stream = event_to_read == REPROC_EVENT_OUT
                               ? REPROC_STREAM_OUT
                               : REPROC_STREAM_ERR;
    r = reproc_read(source->process, stream, buffer, size);
    if (r > 0) {
      buffer[r] = '\0';
      printf("%s", buffer);
    }
    if (r == REPROC_EPIPE) {
      if (source->interests & event_to_read) {
        *event_state |= event_to_read;
      }
      r = 0;
    } else if (r == REPROC_EWOULDBLOCK) {
      source->events -= event_to_read;
      r = 0;
    }
  }
  return r;
}

static int parent(const char *program)
{
  reproc_event_source children[NUM_CHILDREN] = { { 0 } };
  int children_events_done[NUM_CHILDREN] = { 0 };
  int r = -1;
  int i = 0;

  for (i = 0; i < NUM_CHILDREN; i++) {
    reproc_t *process = reproc_new();
    reproc_options options = { 0 };
    const char *args[] = { program, "child", NULL };

    options.nonblocking = true;
    options.redirect.out.type = REPROC_REDIRECT_PIPE;
    options.redirect.err.type = REPROC_REDIRECT_PIPE;
    r = reproc_start(process, args, options);
    if (r < 0) {
      goto finish;
    }

    children[i].process = process;
    children[i].interests = REPROC_EVENT_OUT | REPROC_EVENT_ERR;
  }

  for (;;) {
    bool has_data = true;
    r = reproc_poll(children, NUM_CHILDREN, REPROC_INFINITE);
    if (r < 0) {
      r = r == REPROC_EPIPE ? 0 : r;
      goto finish;
    }
    while (has_data) {
      has_data = false;
      for (i = 0; i < NUM_CHILDREN; i++) {
        int r_out = 0;
        int r_err = 0;
        uint8_t output[4096];
        if (children[i].process == NULL) {
          continue;
        }
        r_out = reproc_pipe_read(children + i, output, sizeof(output) - 1,
                                 REPROC_EVENT_OUT, children_events_done + i);
        r_err = reproc_pipe_read(children + i, output, sizeof(output) - 1,
                                 REPROC_EVENT_ERR, children_events_done + i);
        if (r_out > 0 || r_err > 0) {
          has_data = true;
        }
        if (r_out < 0 || r_err < 0 ||
            children_events_done[i] == children[i].interests) {
          children[i].process = reproc_destroy(children[i].process);
        }
      }
    }
  }

finish:
  for (i = 0; i < NUM_CHILDREN; i++) {
    reproc_destroy(children[i].process);
  }

  if (r < 0) {
    fprintf(stderr, "%s\n", reproc_strerror(r));
  }

  return abs(r);
}

static int child(void)
{
  int ms = 0;
  int i = 0;
  srand(((unsigned int) getpid()));
  for (i = 0; i < 50; ++i) {
    ms = rand() % 20 * 4; // NOLINT
    millisleep(ms);
    fprintf(stdout, "Process %i slept %i milliseconds stdout.\n", getpid(), ms);
    fflush(stdout);
    fprintf(stderr, "Process %i slept %i milliseconds stderr.\n", getpid(), ms);
    fflush(stderr);
  }
  fprintf(stdout, "Process %i finished.\n", getpid());
  fprintf(stderr, "Process %i finished.\n", getpid());
  return EXIT_SUCCESS;
}

// Starts a number of child processes that each sleep a random amount of
// milliseconds before printing a message and exiting. The parent process polls
// each of the child processes and prints their messages to stdout.
int main(int argc, const char **argv)
{
  return argc > 1 && strcmp(argv[1], "child") == 0 ? child() : parent(argv[0]);
}

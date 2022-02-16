#include "../examples/common.h"
#include "assert.h"

static void stop(REPROC_STOP action, int status)
{
  int r = -1;
  const char *argv[] = { get_executable("/stop"), NULL };
  reproc_options options = { 0 };
  reproc_stop_actions stop = { 0 };

  reproc_t *process = reproc_new();
  ASSERT(process);

  r = reproc_start(process, argv, options);
  ASSERT_OK(r);

  r = reproc_wait(process, 50);
  ASSERT(r == REPROC_ETIMEDOUT);

  {
    reproc_stop_action stop_first = { action, 500 };
    stop.first = stop_first;
  }
  r = reproc_stop(process, stop);
  ASSERT_EQ_INT(r, status);

  reproc_destroy(process);
  reproc_free(argv[0]);
}

int main(void)
{
  stop(REPROC_STOP_TERMINATE, REPROC_SIGTERM);
  stop(REPROC_STOP_KILL, REPROC_SIGKILL);
}

#include <stdio.h>
#include <stdlib.h>

#include "../examples/common.h"
#include "assert.h"

int main(void)
{
  const char *argv[] = { get_executable("/pid"), NULL };
  char *output = NULL;
  reproc_sink sink = reproc_sink_string(&output);
  int r = -1;
  reproc_options options = { 0 };

  reproc_t *process = reproc_new();
  ASSERT(process);

  ASSERT(reproc_pid(process) == REPROC_EINVAL);

  r = reproc_start(process, argv, options);
  ASSERT_OK(r);

  r = reproc_drain(process, sink, sink);
  ASSERT_OK(r);
  ASSERT(output != NULL);

  ASSERT(reproc_pid(process) == strtol(output, NULL, 10));

  r = reproc_wait(process, REPROC_INFINITE);
  ASSERT_OK(r);

  ASSERT(reproc_pid(process) == strtol(output, NULL, 10));

  reproc_destroy(process);
  reproc_free(output);
  reproc_free(argv[0]);
}

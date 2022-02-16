#include "../examples/common.h"
#include "assert.h"

int main(void)
{
  const char *argv[] = { get_executable("/deadline"), NULL };
  reproc_options options = { 0 };
  int r = -1;

  options.deadline = 100;
  r = reproc_run(argv, options);
  ASSERT(r == REPROC_SIGTERM);
  reproc_free(argv[0]);
}

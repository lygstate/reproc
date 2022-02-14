#include <stdlib.h>

#include <reproc/run.h>

// Start a process from the arguments given on the command line. Inherit the
// parent's standard streams and allow the process to run for maximum 5 seconds
// before terminating it.
int main(int argc, const char **argv)
{
  int r = -1;
  reproc_options options = { 0 };
  (void) argc;

  options.deadline = 5000;
  r = reproc_run(argv + 1, options);

  if (r < 0) {
    fprintf(stderr, "%s\n", reproc_strerror(r));
  }

  return abs(r);
}

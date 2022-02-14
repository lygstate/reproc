#include <stdlib.h>

#include <reproc/run.h>

// Runs a binary as a child process that prints all its environment variables to
// stdout and exits. Additional environment variables (in the format A=B) passed
// via the command line are added to the child process environment variables.
int main(int argc, const char **argv)
{
  const char *args[] = { RESOURCE_DIRECTORY "/env", NULL };
  int r = -1;
  reproc_options options = { 0 };

  options.env.extra = argv + 1;
  r = reproc_run(args, options);

  (void) argc;

  if (r < 0) {
    fprintf(stderr, "%s\n", reproc_strerror(r));
  }

  return abs(r);
}

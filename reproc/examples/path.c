#include <stdlib.h>

#include <reproc/run.h>

// Redirects the output of the given command to the reproc.out file.
int main(int argc, const char **argv)
{
  int r = -1;
  reproc_options options = { 0 };
  (void) argc;

  options.redirect.path = "reproc.out";
  r = reproc_run(argv + 1, options);

  if (r < 0) {
    fprintf(stderr, "%s\n", reproc_strerror(r));
  }

  return abs(r);
}

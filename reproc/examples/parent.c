#include <stdlib.h>

#include <reproc/reproc.h>

// Forwards the provided command to `reproc_start` and redirects the standard
// streams of the child process to the standard streams of the parent process.
int main(int argc, const char **argv)
{
  reproc_t *process = NULL;
  int r = REPROC_ENOMEM;
  reproc_options options = { 0 };

  if (argc <= 1) {
    fprintf(stderr,
            "No arguments provided. Example usage: ./inherit cmake --help");
    return EXIT_FAILURE;
  }

  process = reproc_new();
  if (process == NULL) {
    goto finish;
  }

  options.redirect.parent = true;
  r = reproc_start(process, argv + 1, options);
  if (r < 0) {
    goto finish;
  }

  r = reproc_wait(process, REPROC_INFINITE);
  if (r < 0) {
    goto finish;
  }

finish:
  reproc_destroy(process);

  if (r < 0) {
    fprintf(stderr, "%s\n", reproc_strerror(r));
  }

  return abs(r);
}

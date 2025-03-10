#include "../examples/common.h"
#include "assert.h"

int main(void)
{
  const char *argv[] = { get_executable("/overflow"), NULL };
  char *output = NULL;
  reproc_sink sink = reproc_sink_string(&output);
  int r = -1;
  reproc_options options = { 0 };

  r = reproc_run_ex(argv, options, sink, sink);
  ASSERT_OK(r);
  ASSERT(output != NULL);

  reproc_free(output);
  reproc_free(argv[0]);
}

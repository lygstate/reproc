#include "../examples/common.h"
#include "assert.h"

int main(void)
{
  const char *argv[] = { get_executable("/env"), NULL };
  const char *envp[] = { "IP=127.0.0.1", "PORT=8080", NULL };
  char *output = NULL;
  reproc_sink sink = reproc_sink_string(&output);
  int r = -1;
  reproc_options options = { 0 };
  const char *current = NULL;
  size_t i = 0;

  options.env.behavior = REPROC_ENV_EMPTY;
  options.env.extra = envp;
  r = reproc_run_ex(argv, options,
                    sink, sink);
  ASSERT_OK(r);
  ASSERT(output != NULL);

  current = output;

  for (i = 0; i < 2; i++) {
    size_t size = strlen(envp[i]);

    ASSERT_GE_SIZE(strlen(current), size);
    ASSERT_EQ_MEM(current, envp[i], size);

    current += size;
    current += *current == '\r';
    current += *current == '\n';
  }

  ASSERT_EQ_SIZE(strlen(current), (size_t) 0);

  reproc_free(output);
  reproc_free(argv[0]);
}

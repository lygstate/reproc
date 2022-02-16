#include <stdlib.h>

#include "../examples/common.h"
#include "assert.h"

static void replace(char *string, char old, char new)
{
  size_t i = 0;
  for (i = 0; i < strlen(string); i++) {
    string[i] = (char) (string[i] == old ? new : string[i]);
  }
}

int main(void)
{
  const char *argv[] = { get_executable("/working-directory"), NULL };
  char *output = NULL;
  reproc_sink sink = reproc_sink_string(&output);
  int r = -1;
  reproc_options options = { 0 };
  char buffer[8192];
  char *module_path = reproc_module_path((void *) (intptr_t) main);
  char *resource_directory = reproc_join_string(module_path,
                                                "/../../resources");

  reproc_path_normalize(buffer, sizeof(buffer), resource_directory,
                        (int) strlen(resource_directory), '/', true);
  memcpy(resource_directory, buffer, strlen(buffer) + 1);
  options.working_directory = resource_directory;
  r = reproc_run_ex(argv,
                    options,
                    sink, sink);
  ASSERT_OK(r);
  ASSERT(output != NULL);

  replace(output, '\\', '/');
  ASSERT_EQ_STR(output, buffer);

  reproc_free(output);
  reproc_free(resource_directory);
  reproc_free(module_path);
  reproc_free(argv[0]);
}

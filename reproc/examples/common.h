#include <stdlib.h>
#include <string.h>

#include <reproc/run.h>

static const char *get_executable(const char *name)
{
  char *module_path = reproc_module_path((void *) (intptr_t) get_executable);
  char *resource_directory = reproc_join_string(module_path,
                                                "/../../resources");
  char *executable = reproc_join_string(resource_directory, name);
  size_t executable_size = strlen(executable);
  char *executable_normalized = malloc(executable_size + 1);
  bool is_win32 = reproc_path_is_win32();
  char path_sep = reproc_path_separator(is_win32);
  reproc_path_normalize(executable_normalized, (int)(executable_size + 1), executable,
                        (int)executable_size, path_sep, is_win32);
  reproc_free(executable);
  reproc_free(resource_directory);
  reproc_free(module_path);
  return executable_normalized;
}

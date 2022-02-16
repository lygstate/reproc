#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include <reproc/util.h>

char *reproc_module_path(const void *function_pointer)
{
  char *result = NULL;
  size_t result_len = 0;
  Dl_info info;
  dladdr(function_pointer, &info);
  result_len = strlen(info.dli_fname);
  result = malloc(result_len + 1);
  memcpy(result, info.dli_fname, result_len + 1);
  return result;
}

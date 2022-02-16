#include "utf.h"

#include <limits.h>
#include <stdlib.h>
#include <windows.h>

#include "error.h"

wchar_t *utf16_from_utf8(const char *string, int size)
{
  int r = -1;
  wchar_t *wstring = NULL;

  ASSERT(string);

  // Determine wstring size (`MultiByteToWideChar` returns the required size if
  // its last two arguments are `NULL` and 0).
  r = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, string, size, NULL, 0);
  if (r == 0) {
    return NULL;
  }

  // `MultiByteToWideChar` does not return negative values so the cast to
  // `size_t` is safe.
  wstring = calloc((size_t) r, sizeof(wchar_t));
  if (wstring == NULL) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }

  // Now we pass our allocated string and its size as the last two arguments
  // instead of `NULL` and 0 which makes `MultiByteToWideChar` actually perform
  // the conversion.
  r = MultiByteToWideChar(CP_UTF8, 0, string, size, wstring, r);
  if (r == 0) {
    free(wstring);
    return NULL;
  }

  return wstring;
}

char *utf8_from_utf16(const wchar_t *src, int src_length, int *out_length)
{
  int length = 0;
  char *output_buffer = NULL;

  ASSERT(src);
  length = WideCharToMultiByte(CP_UTF8, 0, src, src_length, 0, 0, NULL, NULL);
  output_buffer = (char *) malloc((size_t)(length + 1) * sizeof(char));
  if (output_buffer) {
    WideCharToMultiByte(CP_UTF8, 0, src, src_length, output_buffer, length,
                        NULL, NULL);
    output_buffer[length] = '\0';
  }
  if (out_length) {
    *out_length = length;
  }
  return output_buffer;
}

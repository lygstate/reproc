#ifndef _WIN32_WINNT
  #define _WIN32_WINNT 0x0600 // _WIN32_WINNT_VISTA
#elif _WIN32_WINNT < 0x0600
  #error "_WIN32_WINNT must be greater than _WIN32_WINNT_VISTA (0x0600)"
#endif

#include <stdbool.h>
#include <stdlib.h>
#include <windows.h>

#include <reproc/util.h>

#include "utf.h"

bool reproc_windows_version_greator_or_equal(unsigned short wVersion,
                                             unsigned short wServicePackMajor)
{
  OSVERSIONINFOEXW osvi = { 0 };
  DWORDLONG const dwlConditionMask = VerSetConditionMask(
      VerSetConditionMask(VerSetConditionMask(0, VER_MAJORVERSION,
                                              VER_GREATER_EQUAL),
                          VER_MINORVERSION, VER_GREATER_EQUAL),
      VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);

  osvi.dwOSVersionInfoSize = sizeof(osvi);
  osvi.dwMajorVersion = HIBYTE(wVersion);
  osvi.dwMinorVersion = LOBYTE(wVersion);
  osvi.wServicePackMajor = wServicePackMajor;

  return VerifyVersionInfoW(&osvi,
                            VER_MAJORVERSION | VER_MINORVERSION |
                                VER_SERVICEPACKMAJOR,
                            dwlConditionMask) != FALSE;
}

char *reproc_module_path(const void *function_pointer)
{
  HMODULE hm = NULL;
  char *result = NULL;
  DWORD moduleFlags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
  if (GetModuleHandleExW(moduleFlags, (LPCWSTR) function_pointer, &hm) == 0) {
    result = malloc(1);
    result[0] = '\0';
  } else {
    DWORD size = MAX_PATH + 1;
    wchar_t *space = malloc(sizeof(wchar_t) * size);
    DWORD v = 0;
    for (;;) {
      v = GetModuleFileNameW(hm, space, size);
      if (v >= size) {
        size += MAX_PATH;
        space = realloc(space, sizeof(wchar_t) * size);
        continue;
      }
      break;
    }
    result = utf8_from_utf16(space, (int) v, NULL);
    free(space);
  }
  return result;
}

void reproc_call_once(uintptr_t *flag, void (*func)(void))
{
  if (InterlockedCompareExchangePointer((PVOID volatile *) flag, (PVOID) 1,
                                        (PVOID) 0) == 0) {
    (func)();
    InterlockedExchangePointer((PVOID volatile *) flag, (PVOID) 2);
  } else {
    while (*flag == 1) {
      // busy loop!
      SwitchToThread();
    }
  }
}

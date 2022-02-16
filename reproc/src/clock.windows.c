#ifndef _WIN32_WINNT
  #define _WIN32_WINNT 0x0600 // _WIN32_WINNT_VISTA
#elif _WIN32_WINNT < 0x0600
  #error "_WIN32_WINNT must be greater than _WIN32_WINNT_VISTA (0x0600)"
#endif

#include "clock.h"

#include <windows.h>

int64_t now(void)
{
  LARGE_INTEGER count = { 0 };
  LARGE_INTEGER freq = { 0 };
  int64_t time_now = 0;
  QueryPerformanceCounter(&count);
  QueryPerformanceFrequency(&freq);
  time_now = (count.QuadPart / freq.QuadPart) * 1000;
  time_now += (count.QuadPart % freq.QuadPart) * 1000 / freq.QuadPart;
  return time_now;
}

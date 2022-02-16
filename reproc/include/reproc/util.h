#pragma once

#include <reproc/reproc.h>

#define REPROC_ONCE_FLAG_INIT 0

#ifdef __cplusplus
extern "C" {
#endif

/*!
Returns the module path for specific function pointer
*/
REPROC_EXPORT char *reproc_module_path(const void *function_pointer);

/**
 * Check if a char of a path is a path separator.
 *
 * @param path_c: The char to check
 * @param is_win32: the path style is `win32` when true, otherwise `posix`
 *
 * @return if a char of a path is a path separator.
 */
REPROC_EXPORT bool reproc_path_is_separator(char path_c, bool is_win32);

REPROC_EXPORT bool reproc_path_is_win32(void);

/**
 * Get path separator for specified path style
 *
 * @param is_win32: the path style is `win32` when true, otherwise `posix`
 *
 * @return The path separator for specified path style
 */
REPROC_EXPORT char reproc_path_separator(bool is_win32);

/**
 * Get the offset of the basename component in the input path.
 *
 * The implementation should return the offset of the first character after
 * the last path separator found in the path. This is used by the caller to
 * split the path into a directory name and a file name.
 *
 * @param path_p: input zero-terminated path string
 * @param path_size: size of the input path string in bytes,
 *  excluding terminating zero
 * @param is_win32: the path style is `win32` when true, otherwise `posix`
 *
 * @return offset of the basename component in the input path
 */
REPROC_EXPORT int
reproc_path_base(const char *path_p, int path_size, bool is_win32);

/**
 * Evalute the length of root component of a path
 *
 * @param path_p: input zero-terminated path string
 * @param path_size: length of the path string
 * @param is_win32: the path style is `win32` when true, otherwise `posix`
 *
 * @return length of root component of a path if it's a absolute path
 *         0 otherwise
 */
REPROC_EXPORT int
reproc_path_root(const char *path_p, int path_size, bool is_win32);

REPROC_EXPORT int reproc_path_normalize(char *output,
                                        int output_size,
                                        const char *path_p,
                                        int path_size,
                                        char path_separator_used,
                                        bool is_win32);

REPROC_EXPORT char *reproc_join_string(const char *a, const char *b);

REPROC_EXPORT void reproc_call_once(uintptr_t *flag, void (*func)(void));

/*! Calls `free` on `ptr` and returns `NULL`. Use this function to free memory
allocated by `reproc_sink_string` or `reproc_join_string` or
`reproc_module_path`. This avoids issues with allocating across module (DLL)
boundaries on Windows. */
REPROC_EXPORT void *reproc_free(const void *ptr);

#ifdef _WIN32
REPROC_EXPORT bool
reproc_windows_version_greator_or_equal(unsigned short wVersion,
                                        unsigned short wServicePackMajor);
#endif

#ifdef __cplusplus
}
#endif

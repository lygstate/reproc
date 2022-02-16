#include <stdlib.h>
#include <string.h>

#include <reproc/util.h>

char *reproc_join_string(const char *a, const char *b)
{
  size_t len_a = strlen(a);
  size_t len_b = strlen(b);
  char *out = malloc(len_a + len_b + 1);
  memcpy(out, a, len_a + 1);
  memcpy(out + len_a, b, len_b + 1);
  return out;
}

bool reproc_path_is_separator(char path_c, bool is_win32)
{
  if (path_c == '/' || (is_win32 && path_c == '\\')) {
    return true;
  }
  return false;
}

bool reproc_path_is_win32(void)
{
#if defined(_WIN32)
  return true;
#else
  return false;
#endif
}

char reproc_path_separator(bool is_win32)
{
  if (is_win32) {
    return '\\';
  }
  return '/';
}

int reproc_path_base(const char *path_p, int path_size, bool is_win32)
{
  const char *end_p = path_p + path_size;
  while (end_p > path_p) {
    if (reproc_path_is_separator(end_p[-1], is_win32)) {
      return (int) (end_p - path_p);
    }
    end_p--;
  }
  return 0;
}

int reproc_path_root(const char *path_p, int path_size, bool is_win32)
{
  char path0 = '\0';
  bool path0_is_separator = false;
  if (path_p == NULL) {
    return 0;
  }

  if (path_size == 0) {
    return 0;
  }

  /* Path size >= 1 */
  path0 = path_p[0];

  if (!is_win32) {
    /* Posix path style */
    if (reproc_path_is_separator(path0, is_win32)) {
      return 1;
    }
    return 0;
  }

  path0_is_separator = reproc_path_is_separator(path0, is_win32);
  if (path_size == 1) {
    return path0_is_separator ? 1 : 0;
  }

  /* Path size >= 2 */
  if (((path0 >= 'a' && path0 <= 'z') || (path0 >= 'A' && path0 <= 'Z')) &&
      path_p[1] == ':') {
    /* It's `c:` `d:` `D:`, not absolute path */
    if (path_size == 2) {
      return 2;
    }
    /* Path size >= 3, root path is the style of `C:\`, `c:/`, absolute path */
    if (reproc_path_is_separator(path_p[2], is_win32)) {
      return 3;
    }
    /* Path with windows root such as `C:t` `C:0` `C:..\some_path`,
       not absolute path */
    return 2;
  }
  if (!path0_is_separator) {
    /* not a absolute path */
    return 0;
  }

  /* Check if this path is an UNC absolute path, according to
   * https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-files */
  {
    int path_i = 0;
    bool has_server_name = false;
    bool has_share_name = false;
    /* Skipping all the path separators */
    while (path_i < path_size &&
           reproc_path_is_separator(path_p[path_i], is_win32)) {
      ++path_i;
    }
    /* It's a absolute path with a single leading separator if the leading
     * separator is not 2 such as '///x', '/b', `\\\\\\t`
     */
    if (path_i != 2) {
      return 1;
    }
    /* Skipping the UNC domain name or server name */
    while (path_i < path_size &&
           !reproc_path_is_separator(path_p[path_i], is_win32)) {
      ++path_i;
      has_server_name = true;
    }
    /* Skipping all the path separators */
    while (path_i < path_size &&
           reproc_path_is_separator(path_p[path_i], is_win32)) {
      ++path_i;
    }
    /* Skipping the UNC share name */
    while (path_i < path_size &&
           !reproc_path_is_separator(path_p[path_i], is_win32)) {
      ++path_i;
      has_share_name = true;
    }
    /**
     * Skipping the leading path separator
     * There might be a separator at the network share root path end,
     * include that as well, it will mark the path as absolute, such as
     * `\\localhost\c$\`
     */
    if (path_i < path_size &&
        reproc_path_is_separator(path_p[path_i], is_win32)) {
      ++path_i;
    }
    if (has_server_name && has_share_name) {
      /* It's a UNC absolute path */
      return path_i;
    }
    /* It's a absolute path with a single leading separator */
    return 1;
  }
}

struct reproc_path_normalize_segment {
  char *output_path_c;
  int path_size_normalized;
  bool separator_appended;
  char path_separator_used;
  int dot_count;
  int char_count;
};

static void
_reproc_path_segment_append(struct reproc_path_normalize_segment *segment,
                            bool is_separator,
                            char c)
{
  if (!is_separator || (is_separator && !segment->separator_appended)) {
    segment->path_size_normalized += 1;
    segment->separator_appended = is_separator;
    if (segment->output_path_c != NULL) {
      segment->output_path_c -= 1;
      if (is_separator) {
        segment->output_path_c[0] = segment->path_separator_used;
      } else {
        segment->output_path_c[0] = c;
      }
    }
  }
}

static int _reproc_get_dot_count(struct reproc_path_normalize_segment *segment)
{
  if (segment->dot_count == segment->char_count) {
    return segment->dot_count;
  }
  return -1;
}

int reproc_path_normalize(char *output,
                          int output_size,
                          const char *path_p,
                          int path_size,
                          char path_separator_used,
                          bool is_win32)
{
  int path_root_length = 0;
  struct reproc_path_normalize_segment segment = { 0 };
  bool is_absolute_path = false;

  segment.path_separator_used = path_separator_used;

  /* https://docs.microsoft.com/en-us/windows/win32/fileio/naming-a-file */

  if (!reproc_path_is_separator(path_separator_used, is_win32)) {
    /* Invalid path separator used */
    return -1;
  }
  if (path_p == NULL) {
    if (path_size != 0) {
      /* Invalid path size */
      return -1;
    }
    path_p = "";
  }

  path_root_length = reproc_path_root(path_p, path_size, is_win32);
  while (path_root_length > 0 &&
         reproc_path_is_separator(path_p[path_root_length - 1], is_win32)) {
    /* Trim the trailling '///' of root path components */
    path_root_length -= 1;
    is_absolute_path = true;
  }
  if (path_root_length >= 5) {
    is_absolute_path = true;
  }

  for (;;) {
    enum state_t { SEGMENT, SLASH, ROOT } state = SEGMENT;
    size_t skip_segments = 0;
    bool segment_ready = false;
    bool dot_slash = false;
    bool from_slash = false;
    int i = path_size - 1;

    segment.separator_appended = false;
    segment.path_size_normalized = 0;
    segment.dot_count = 0;
    segment.char_count = 0;
    for (;;) {
      char c = '.';
      bool is_separator = false;
      if (i == path_root_length - 1) {
        if (state == SEGMENT) {
          int dot_count = _reproc_get_dot_count(&segment);
          /* zero dot count means empty path such as `` or E: */
          bool is_normal_segment = dot_count < 0 || dot_count > 2;
          state = ROOT;
          if (is_normal_segment) {
            if (skip_segments) {
              skip_segments -= 1;
            }
          } else {
            if (dot_count <= 1) {
              if (!is_absolute_path && (segment.separator_appended ||
                                        segment.path_size_normalized == 0)) {
                if (!dot_slash && from_slash) {
                  _reproc_path_segment_append(&segment, true, '/');
                }
                _reproc_path_segment_append(&segment, false, '.');
              }
            }
            if (dot_count == 2) {
              skip_segments += 1;
            }
          }
        }
        if (is_absolute_path) {
          /* The skip_segments have no effect for absolute path */
          state = SLASH;
        }
        if (state == SLASH) {
          _reproc_path_segment_append(&segment, true, '/');
          state = ROOT;
        } else if (state == ROOT) {
          size_t si = 0;
          for (si = 0; si < skip_segments; ++si) {
            _reproc_path_segment_append(&segment, true, '/');
            _reproc_path_segment_append(&segment, false, '.');
            _reproc_path_segment_append(&segment, false, '.');
          }
        }
        skip_segments = 0;
      }
      if (i >= 0) {
        c = path_p[i];
        i -= 1;
      } else { /* i < 0 */
        if (path_root_length >= 5) {
          /* This is an UNC path, the path slash should be `//` */
          segment.separator_appended = false;
          _reproc_path_segment_append(&segment, true, '/');
        }
        break;
      }
      is_separator = reproc_path_is_separator(c, is_win32);
      switch (state) {
        case SEGMENT:
          if (is_separator) {
            int dot_count = _reproc_get_dot_count(&segment);
            if (dot_count == 1 || dot_count == 2) {
              if (!from_slash) {
                dot_slash = true;
              }
              if (dot_count == 2) {
                skip_segments++;
              }
            } else {
              if (skip_segments) {
                skip_segments--;
              }
            }
            state = SLASH;
            continue;
          }
          if (c == '.') {
            segment.dot_count += 1;
          }
          segment.char_count += 1;
          if (!segment_ready) {
            if (segment.char_count > 2 ||
                (segment.char_count != segment.dot_count)) {
              segment_ready = true;
              i += segment.char_count;
              segment.char_count = 0;
              segment.dot_count = 0;
            }
            continue;
          }
          if (skip_segments) {
            continue;
          }
          if (!dot_slash && from_slash) {
            _reproc_path_segment_append(&segment, true, '/');
          }
          dot_slash = false;
          from_slash = false;
          break;
        case SLASH:
          if (!is_separator) {
            from_slash = true;
            i += 1;
            segment.dot_count = 0;
            segment.char_count = 0;
            segment_ready = false;
            state = SEGMENT;
          }
          continue;
        case ROOT:
          break;
      } /* switch (state) */
      _reproc_path_segment_append(&segment, is_separator, c);
    }
    if (segment.output_path_c == NULL) {
      if (segment.path_size_normalized >= 0 &&
          segment.path_size_normalized <= output_size && output != NULL) {
        segment.output_path_c = output + segment.path_size_normalized;
        if (segment.path_size_normalized < output_size) {
          *segment.output_path_c = '\0';
        }
        continue;
      }
    }
    break;
  }
  return segment.path_size_normalized;
}

void *reproc_free(const void *ptr)
{
  free((void *) ptr);
  return NULL;
}

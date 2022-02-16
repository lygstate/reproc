#include <reproc/run.h>

#include "assert.h"

static void
test_normalize_path(bool is_win32, const char *path_c, const char *expected)
{
  char buffer[1024];
  int out_size = reproc_path_normalize(buffer, sizeof(buffer), path_c,
                                       (int) strlen(path_c),
                                       reproc_path_separator(is_win32),
                                       is_win32);
  ASSERT_EQ_STR(buffer, expected);
  ASSERT_EQ_INT(out_size, (int) strlen(expected));
}

/* https://github.com/nodejs/node/blob/master/test/parallel/test-path-normalize.js
 */
static void test_nodejs()
{
  /* Win32 */
  test_normalize_path(true, "./fixtures///b/../b/c.js", "fixtures\\b\\c.js");
  test_normalize_path(true, "/foo/../../../bar", "\\bar");
  test_normalize_path(true, "a//b//../b", "a\\b");
  test_normalize_path(true, "a//b//./c", "a\\b\\c");
  test_normalize_path(true, "a//b//.", "a\\b");
  test_normalize_path(true, "//server/share/dir/file.ext",
                      "\\\\server\\share\\dir\\file.ext");
  test_normalize_path(true, "/a/b/c/../../../x/y/z", "\\x\\y\\z");
  test_normalize_path(true, "C:", "C:.");
  test_normalize_path(true, "C:..\\abc", "C:..\\abc");
  test_normalize_path(true, "C:..\\..\\abc\\..\\def", "C:..\\..\\def");
  test_normalize_path(true, "C:\\.", "C:\\");
  test_normalize_path(true, "file:stream", "file:stream");
  test_normalize_path(true, "bar\\foo..\\..\\", "bar\\");
  test_normalize_path(true, "bar\\foo..\\..", "bar");
  test_normalize_path(true, "bar\\foo..\\..\\baz", "bar\\baz");
  test_normalize_path(true, "bar\\foo..\\", "bar\\foo..\\");
  test_normalize_path(true, "bar\\foo..", "bar\\foo..");
  test_normalize_path(true, "..\\foo..\\..\\..\\bar", "..\\..\\bar");

  test_normalize_path(true, "..\\...\\..\\.\\...\\..\\..\\bar", "..\\..\\bar");
  test_normalize_path(true, "../../../foo/../../../bar",
                      "..\\..\\..\\..\\..\\bar");
  test_normalize_path(true, "../../../foo/../../../bar/../../",
                      "..\\..\\..\\..\\..\\..\\");
  test_normalize_path(true, "../foobar/barfoo/foo/../../../bar/../../",
                      "..\\..\\");
  test_normalize_path(true, "../.../../foobar/../../../bar/../../baz",
                      "..\\..\\..\\..\\baz");
  test_normalize_path(true, "foo/bar\\baz", "foo\\bar\\baz");

  /* Posix */
  test_normalize_path(false, "./fixtures///b/../b/c.js", "fixtures/b/c.js");
  test_normalize_path(false, "/foo/../../../bar", "/bar");
  test_normalize_path(false, "a//b//../b", "a/b");
  test_normalize_path(false, "a//b//./c", "a/b/c");
  test_normalize_path(false, "a//b//.", "a/b");
  test_normalize_path(false, "/a/b/c/../../../x/y/z", "/x/y/z");
  test_normalize_path(false, "///..//./foo/.//bar", "/foo/bar");
  test_normalize_path(false, "bar/foo../../", "bar/");
  test_normalize_path(false, "bar/foo../..", "bar");
  test_normalize_path(false, "bar/foo../../baz", "bar/baz");
  test_normalize_path(false, "bar/foo../", "bar/foo../");
  test_normalize_path(false, "bar/foo..", "bar/foo..");
  test_normalize_path(false, "../foo../../../bar", "../../bar");
  test_normalize_path(false, "../.../.././.../../../bar", "../../bar");
  test_normalize_path(false, "../../../foo/../../../bar", "../../../../../bar");
  test_normalize_path(false, "../../../foo/../../../bar/../../",
                      "../../../../../../");
  test_normalize_path(false, "../foobar/barfoo/foo/../../../bar/../../",
                      "../../");
  test_normalize_path(false, "../.../../foobar/../../../bar/../../baz",
                      "../../../../baz");
  test_normalize_path(false, "foo/bar\\baz", "foo/bar\\baz");
}

int main(void)
{
  char buffer[1024];
  int out_size = 0;

  {
    ASSERT_EQ_INT(reproc_path_is_separator('\\', true), true);
    ASSERT_EQ_INT(reproc_path_is_separator('/', true), true);
    ASSERT_EQ_INT(reproc_path_is_separator('x', true), false);

    ASSERT_EQ_INT(reproc_path_is_separator('\\', false), false);
    ASSERT_EQ_INT(reproc_path_is_separator('/', true), true);
    ASSERT_EQ_INT(reproc_path_is_separator('x', true), false);
  }

  {
    const char test_path[] = "//./";
    ASSERT_EQ_INT(reproc_path_root(test_path, sizeof(test_path) - 1, true), 1);
  }
  {
    const char test_path[] = "//?/";
    ASSERT_EQ_INT(reproc_path_root(test_path, sizeof(test_path) - 1, true), 1);
  }
  {
    const char test_path[] = "//?/C";
    ASSERT_EQ_INT(reproc_path_root(test_path, sizeof(test_path) - 1, true), 5);
  }
  {
    const char test_path[] = "//?////";
    ASSERT_EQ_INT(reproc_path_root(test_path, sizeof(test_path) - 1, true), 1);
  }
  {
    const char test_path[] = "///////?/x///";
    ASSERT_EQ_INT(reproc_path_root(test_path, sizeof(test_path) - 1, true), 1);
  }
  {
    const char test_path[] = "\\\\..\\\\\\\\.";
    ASSERT_EQ_INT(reproc_path_root(test_path, sizeof(test_path) - 1, true), 9);
  }
  {
    const char test_path[] = "\\\\..\\\\\\\\.\\bbc";
    ASSERT_EQ_INT(reproc_path_root(test_path, sizeof(test_path) - 1, true), 10);
  }
  {
    const char test_path[] = "/////bbc/def/";
    ASSERT_EQ_INT(reproc_path_root(test_path, sizeof(test_path) - 1, true), 1);
  }

  test_nodejs();
  test_normalize_path(true, "bar\\foo..\\abc", "bar\\foo..\\abc");
  test_normalize_path(true, "a//b//../../../../b", "..\\..\\b");
  test_normalize_path(true, "/////.//bbc/", "\\bbc\\");
  test_normalize_path(true, "\\\\..\\\\\\\\.\\..\\", "\\\\..\\.\\");
  test_normalize_path(true, "//./.", "\\\\.\\.\\");
  test_normalize_path(true, "\\\\..\\\\\\\\.", "\\\\..\\.\\");
  test_normalize_path(true, "\\/..\\\\\\\\.x\\aabc\\ddd",
                      "\\\\..\\.x\\aabc\\ddd");
  test_normalize_path(true, "\\/..\\\\\\\\.x\\aabc\\..\\..\\..\\..\\ddd",
                      "\\\\..\\.x\\ddd");
  test_normalize_path(true, "\\..\\..\\resources\\argv", "\\resources\\argv");
  test_normalize_path(true, ".\\..\\..\\resources\\argv",
                      "..\\..\\resources\\argv");
  test_normalize_path(true, "..\\..\\resources\\argv",
                      "..\\..\\resources\\argv");
  test_normalize_path(true, "E:", "E:.");
  test_normalize_path(true, "E:.", "E:.");
  test_normalize_path(true, "E:\\a", "E:\\a");

  test_normalize_path(true, "E:\\test\\abc\\r.txt\\..\\..\\resources\\argv",
                      "E:\\test\\resources\\argv");
  test_normalize_path(true, "E:\\..\\..\\resources\\argv",
                      "E:\\resources\\argv");
  test_normalize_path(true, "E:./////", "E:.\\");
  test_normalize_path(true, "././././//", ".\\");
  test_normalize_path(true, "././././//.", ".");
  test_normalize_path(true, ".", ".");

  {
    out_size = reproc_path_normalize(buffer, sizeof(buffer), NULL, 0, '/',
                                     true);
    ASSERT_EQ_INT(out_size, 1);
    ASSERT_EQ_STR(buffer, ".");
  }
  {
    out_size = reproc_path_normalize(buffer, sizeof(buffer), NULL, 0, '\\',
                                     true);
    ASSERT_EQ_INT(out_size, 1);
    ASSERT_EQ_STR(buffer, ".");
  }
  {
    out_size = reproc_path_normalize(buffer, sizeof(buffer), NULL, 0, 'x',
                                     true);
    ASSERT_EQ_INT(out_size, -1);
  }
  {
    out_size = reproc_path_normalize(buffer, sizeof(buffer), NULL, 1, '\\',
                                     true);
    ASSERT_EQ_INT(out_size, -1);
  }
  {
    const char *test_path = "";
    out_size = reproc_path_normalize(buffer, sizeof(buffer), test_path, 0, '\\',
                                     true);
    ASSERT_EQ_INT(out_size, 1);
    ASSERT_EQ_STR(buffer, ".");
  }
  return 0;
}

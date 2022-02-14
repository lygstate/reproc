#include <doctest.h>

#include <reproc/reproc.h>
#include <liblocate/liblocate.h>

#include <array>

TEST_CASE("working-directory")
{
  reproc_t noop;

  REPROC_ERROR error = REPROC_SUCCESS;
  const char* errorResult = reproc_strerror(error);
  INFO(errorResult);

  std::string working_directory = getExecutablePath() + "/../../resources";
  std::string noopResourcePath = getExecutablePath() + "/../../resources/noop";
  std::array<const char *, 2> argv = { noopResourcePath.c_str(), nullptr };

  error = reproc_start(&noop, argv.data(), nullptr, working_directory.c_str());
  REQUIRE(!error);

  error = reproc_wait(&noop, REPROC_INFINITE);
  REQUIRE(!error);
  REQUIRE((reproc_exit_status(&noop) == 0));

  reproc_destroy(&noop);
}

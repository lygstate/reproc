#include <doctest.h>

#include <reproc/reproc.h>
#include <liblocate/liblocate.h>

#include <array>

TEST_CASE("stop")
{
  reproc_t infinite;

  REPROC_ERROR error = REPROC_SUCCESS;
  const char *errorResult = reproc_strerror(error);
  INFO(errorResult);

  std::string infiniteResourcePath = getExecutablePath() + "/../../resources/infinite";

  std::array<const char *, 2> argv = {infiniteResourcePath.c_str(), nullptr};

  error = reproc_start(&infinite, argv.data(), nullptr, nullptr);
  REQUIRE(!error);

  // Waiting more time for infinite been started, so kill won't crash
  error = reproc_wait(&infinite, 500);
  REQUIRE(error == REPROC_ERROR_WAIT_TIMEOUT);

  SUBCASE("terminate")
  {
    error = reproc_terminate(&infinite);
    REQUIRE(!error);
  }

  SUBCASE("kill")
  {
    error = reproc_kill(&infinite);
    REQUIRE(!error);
  }

  error = reproc_wait(&infinite, 50);
  REQUIRE(!error);

  reproc_destroy(&infinite);
}

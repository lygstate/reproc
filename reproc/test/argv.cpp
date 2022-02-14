#include <doctest.h>

#include <reproc/reproc.h>
#include <liblocate/liblocate.h>

#include <algorithm>
#include <array>
#include <string>

TEST_CASE("argv")
{
  reproc_t process;

  REPROC_ERROR error = REPROC_SUCCESS;
  const char* errorResult = reproc_strerror(error);
  INFO(errorResult);

  std::string argvResourcePath = getExecutablePath() + "/../../resources/argv";

  std::array<const char *, 4> argv = {argvResourcePath.c_str(), "argument 1",
                                       "argument 2", nullptr };
  error = reproc_start(&process, argv.data(), nullptr, nullptr);
  REQUIRE(!error);

  std::string output;

  static const unsigned int BUFFER_SIZE = 1024;
  std::array<uint8_t, BUFFER_SIZE> buffer = {};

  while (true) {
    unsigned int bytes_read = 0;
    error = reproc_read(&process, REPROC_STREAM_OUT, buffer.data(), BUFFER_SIZE,
                        &bytes_read);
    if (error != REPROC_SUCCESS) {
      break;
    }

    output.append(reinterpret_cast<const char *>(buffer.data()), bytes_read);
  }

  REQUIRE(error == REPROC_ERROR_STREAM_CLOSED);
  REQUIRE(std::count(output.begin(), output.end(), '\n') == argv.size() - 1);
}

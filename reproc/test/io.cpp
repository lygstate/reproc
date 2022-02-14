#include <doctest.h>

#include <reproc/reproc.h>
#include <liblocate/liblocate.h>

#include <array>
#include <string>

void io(const char *program, REPROC_STREAM stream)
{
  REPROC_ERROR error = REPROC_SUCCESS;
  const char* errorResult = reproc_strerror(error);
  INFO(errorResult);

  reproc_t io;
  std::array<const char *, 2> argv = { program, nullptr };

  error = reproc_start(&io, argv.data(), nullptr, nullptr);
  REQUIRE(!error);

  std::string message = "reproc stands for REdirected PROCess";
  auto size = static_cast<unsigned int>(message.length());

  unsigned int bytes_written = 0;
  error = reproc_write(&io, reinterpret_cast<const uint8_t *>(message.data()),
                       size, &bytes_written);
  REQUIRE(!error);

  reproc_close(&io, REPROC_STREAM_IN);

  std::string output;
  static const unsigned int BUFFER_SIZE = 1024;
  std::array<uint8_t, BUFFER_SIZE> buffer = {};

  while (true) {
    unsigned int bytes_read = 0;
    error = reproc_read(&io, stream, buffer.data(), BUFFER_SIZE, &bytes_read);
    if (error != REPROC_SUCCESS) {
      break;
    }

    output.append(reinterpret_cast<const char *>(buffer.data()), bytes_read);
  }

  REQUIRE_EQ(output, message);

  error = reproc_wait(&io, REPROC_INFINITE);
  REQUIRE(!error);
  REQUIRE((reproc_exit_status(&io) == 0));

  reproc_destroy(&io);
}

TEST_CASE("io")
{
  std::string stdoutResourcePath = getExecutablePath() + "/../../resources/stdout";
  std::string stderrResourcePath = getExecutablePath() + "/../../resources/stderr";

  io(stdoutResourcePath.c_str(), REPROC_STREAM_OUT);
  io(stderrResourcePath.c_str(), REPROC_STREAM_ERR);
}

add_library(reproc STATIC)

target_sources(reproc PRIVATE src/common.c)
target_include_directories(reproc PUBLIC include ../liblocate/include)

if(WIN32)
  target_sources(reproc PRIVATE
    src/windows/error.c
    src/windows/handle.c
    src/windows/pipe.c
    src/windows/process.c
    src/windows/reproc.c
  )
elseif(UNIX)
  target_sources(reproc PRIVATE
    src/posix/error.c
    src/posix/fd.c
    src/posix/path.c
    src/posix/pipe.c
    src/posix/process.c
    src/posix/reproc.c
  )
endif()

include(CheckSymbolExists)

if(MSVC)
  target_compile_definitions(reproc PRIVATE
    _CRT_SECURE_NO_WARNINGS
    # Remove unneeded stuff from windows.h.
    WIN32_LEAN_AND_MEAN
    VC_EXTRALEAN
    NOMINMAX
  )
  target_compile_options(reproc PRIVATE /TP)
elseif(UNIX)
  # Check if `pipe2` is available.
  list(APPEND CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE)
  check_symbol_exists(pipe2 unistd.h REPROC_PIPE2_FOUND)
  list(REMOVE_AT CMAKE_REQUIRED_DEFINITIONS -1)

  target_compile_definitions(reproc PRIVATE
    _GNU_SOURCE # Needed for `pipe2` and `kill`.
    $<$<BOOL:${REPROC_PIPE2_FOUND}>:PIPE2_FOUND>
  )
endif()

function(reproc_add_test_helper TARGET)
  add_executable(reproc-${TARGET})

  reproc_add_common(reproc-${TARGET} CXX 11 resources)
  target_sources(reproc-${TARGET} PRIVATE resources/${TARGET}.cpp)
  set_target_properties(reproc-${TARGET} PROPERTIES OUTPUT_NAME ${TARGET})

endfunction()

# Helper binaries required by reproc's tests.
reproc_add_test_helper(argv)
reproc_add_test_helper(environment)
reproc_add_test_helper(stdout)
reproc_add_test_helper(stderr)
reproc_add_test_helper(infinite)
reproc_add_test_helper(noop)

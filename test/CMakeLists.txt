set(gtest_force_shared_crt ON)
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0)
  FetchContent_MakeAvailable(googletest)
endif()

if(RSTD_USE_ASAN)
  add_compile_options(-g -fsanitize=address -fsanitize=undefined
                      -fsanitize-recover=address)

  add_link_options(-fsanitize=address -fsanitize=undefined
                   -fsanitize-recover=address)
endif()

set(
  RSTD_TEST_SOURCES
  trait.cpp
  result.cpp
  compile.cpp
  option.cpp
  clone.cpp
  convert.cpp
  enum.cpp
  alloc/rc.cpp
  alloc/str.cpp
  alloc/boxed.cpp
  alloc/vec.cpp
  alloc/sync.cpp
  alloc/string.cpp
  iter/iterator.cpp
  sys/sync/mutex/futex.cpp
  thread/thread.cpp
  num/nonzero.cpp
  thread.cpp
  async.cpp
  bytes.cpp
  sync/mpsc.cpp
  time.cpp
  fmt.cpp
  io.cpp
  net.cpp
  env.cpp
  char.cpp
  floats.cpp
  ints.cpp
  str.cpp
  ffi/os_str.cpp
  path.cpp
  log.cpp)

if(UNIX)
  list(
    APPEND
    RSTD_TEST_SOURCES
    sys/sync/mutex/pthread.cpp
    process.cpp
    sys_fd.cpp
    fs.cpp)
endif()

add_executable(rstd_test ${RSTD_TEST_SOURCES})

target_sources(
  rstd_test
  PRIVATE FILE_SET modules TYPE CXX_MODULES FILES log_module_check.cppm)

target_link_libraries(rstd_test PRIVATE rstd::rstd GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(rstd_test)

if(OFF)
add_executable(nostd nostd/main.cpp)
target_link_libraries(nostd PRIVATE rstd::nostd)
target_link_options(nostd PRIVATE -nostdlib++)
endif()

set(gtest_force_shared_crt ON)
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1)
  FetchContent_MakeAvailable(googletest)
endif()

if(RSTD_USE_ASAN)
  add_compile_options(-g -fsanitize=address -fsanitize=undefined
                      -fsanitize-recover=address)

  add_link_options(-fsanitize=address -fsanitize=undefined
                   -fsanitize-recover=address)
endif()

add_executable(
  rstd_test
  trait.cpp
  result.cpp
  compile.cpp
  option.cpp
  clone.cpp
  convert.cpp
  alloc/rc.cpp
  alloc/str.cpp
  alloc/boxed.cpp
  alloc/sync.cpp
  alloc/string.cpp
  sys/sync/mutex/futex.cpp
  sys/sync/mutex/pthread.cpp
  thread/thread.cpp
  num/nonzero.cpp
  thread.cpp
  )

target_link_libraries(rstd_test PRIVATE rstd::rstd GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(rstd_test)

if(OFF)
add_executable(nostd nostd/main.cpp)
target_link_libraries(nostd PRIVATE rstd::rstd)
target_link_options(nostd PRIVATE -nostdlib++)
endif()
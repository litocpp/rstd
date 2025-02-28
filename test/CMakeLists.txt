

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

add_executable(trait_test test.cpp example.cpp)

target_link_libraries(trait_test PRIVATE rstd::trait GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(trait_test)

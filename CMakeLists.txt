cmake_minimum_required(VERSION 3.28)
project(
  rstd
  VERSION 0.1.0
  DESCRIPTION "Rust-like std for C++"
  LANGUAGES CXX)

option(RSTD_BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(RSTD_BUILD_BENCHMARKS "Build rstd benchmark executables" OFF)
option(RSTD_BENCHMARKS_IN_CTEST "Register benchmark smoke tests in ctest" OFF)
option(RSTD_USE_ASAN "asan" OFF)

set(WARN_OPTS -Wall -Wpedantic)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND WARN_OPTS -Werror)
endif()
set(WARN_CLANG_OPTS -Wno-gnu-statement-expression -Wno-deprecated-declarations -fsized-deallocation)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:${WARN_OPTS}>")
  add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,Clang>:${WARN_CLANG_OPTS}>")
  add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-fno-rtti;-fno-exceptions>")
endif()



if(RSTD_BUILD_TESTING OR RSTD_BENCHMARKS_IN_CTEST)
  include(CTest)
endif()

add_subdirectory(src)

if(RSTD_BUILD_BENCHMARKS)
  add_subdirectory(bench)
endif()

# Add installation rules
if(PROJECT_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  install(
    TARGETS rstd.std rstd.runtime rstd.basic rstd.core rstd.alloc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(
    TARGETS rstd.argparse
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILE_SET modules DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/rstd/modules/argparse)
endif()

# Testing configuration
if(RSTD_BUILD_TESTING AND BUILD_TESTING)
  add_subdirectory(test)
endif()

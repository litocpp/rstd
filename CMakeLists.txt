cmake_minimum_required(VERSION 3.28)
project(
  rstd
  VERSION 0.1.0
  DESCRIPTION "Rust-like std for C++"
  LANGUAGES CXX)

option(RSTD_BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
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



add_subdirectory(src)

# Add installation rules
if(PROJECT_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  install(
    TARGETS rstd
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

# Testing configuration
if(RSTD_BUILD_TESTING)
  include(CTest)

  if(BUILD_TESTING)
    add_subdirectory(test)
  endif()
endif()

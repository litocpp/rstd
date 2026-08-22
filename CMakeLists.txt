cmake_minimum_required(VERSION 3.28)
project(
  rstd
  VERSION 0.1.0
  DESCRIPTION "Rust-like std for C++"
  LANGUAGES CXX)

if(WIN32
   AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
   AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"
   AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -rtlib=compiler-rt --print-libgcc-file-name
    RESULT_VARIABLE RSTD_COMPILER_RT_RESULT
    OUTPUT_VARIABLE RSTD_COMPILER_RT_PATH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT RSTD_COMPILER_RT_RESULT EQUAL 0 OR NOT EXISTS "${RSTD_COMPILER_RT_PATH}")
    message(FATAL_ERROR "Unable to locate the Clang compiler runtime library")
  endif()
  get_filename_component(RSTD_COMPILER_RT_LIBRARY "${RSTD_COMPILER_RT_PATH}" NAME_WLE)
endif()

option(RSTD_USE_ASAN "Build rstd and its consumers with AddressSanitizer" OFF)
option(RSTD_CHECK_INTEGER_OVERFLOW
       "Enable integer overflow checks in non-Debug builds" OFF)

if(RSTD_USE_ASAN)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "RSTD_USE_ASAN requires Clang or GCC")
  endif()
  add_library(rstd.asan INTERFACE)
  target_compile_options(rstd.asan INTERFACE -fsanitize=address
                                             -fno-omit-frame-pointer)
  target_link_options(rstd.asan INTERFACE -fsanitize=address)
endif()

set(ALL_OPTS
  -Wall
  -Wpedantic
  -fno-rtti
  -fno-exceptions
)
set(GNU_OPTS
  -Werror
  -Wno-attributes=clang::lifetimebound
)
set(CLANG_OPTS
  -Wno-gnu-statement-expression
  -Wno-deprecated-declarations
  -Wunknown-attributes
  -fsized-deallocation
)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,GNU>:${GNU_OPTS}>")
  add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,Clang>:${CLANG_OPTS}>")
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:${ALL_OPTS}>")
endif()

add_subdirectory(src)

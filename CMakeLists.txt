cmake_minimum_required(VERSION 3.28)
project(
  rstd
  VERSION 0.1.0
  DESCRIPTION "Rust-like std for C++"
  LANGUAGES CXX)

option(RSTD_BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(RSTD_USE_ASAN "asan" OFF)

set(WARN_OPTS -Wall -Wpedantic -Werror)
set(WARN_CLANG_OPTS -Wno-gnu-statement-expression)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:${WARN_OPTS}>")
  add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,Clang>:${WANR_CLANG_OPTS}>")
endif()

if(OFF)
add_compile_options(-nostdlib++)
add_link_options(-nostdlib++)
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

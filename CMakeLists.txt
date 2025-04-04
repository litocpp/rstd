cmake_minimum_required(VERSION 3.28)
project(
  rstd
  VERSION 0.1.0
  DESCRIPTION "Rust-like std for C++"
  LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

option(RC_BUILD_TESTS "Build tests" ${PROJECT_IS_TOP_LEVEL})

set(SYNC_SOURCE_FILES)
set(SYNC_MODULE_FILES)
set(ALLOC_SOURCE_FILES)
set(ALLOC_MODULE_FILES)

add_subdirectory(src/core)
add_subdirectory(src/sys)
add_subdirectory(src/alloc)
add_subdirectory(src/sync)

add_library(rstd)
add_library(rstd::rstd ALIAS rstd)
target_compile_features(rstd PUBLIC cxx_std_20)
target_link_libraries(rstd PUBLIC rstd.core)

target_sources(rstd PRIVATE ${ALLOC_SOURCE_FILES})
target_sources(
  rstd
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         BASE_DIRS
         src
         FILES
         src/mod.cppm
         src/error.cppm
         ${SYNC_MODULE_FILES}
         ${ALLOC_MODULE_FILES}
         )

# Add installation rules
if(PROJECT_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  install(
    TARGETS rstd
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

# Testing configuration
if(RC_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(test)
endif()

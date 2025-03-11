cmake_minimum_required(VERSION 3.28)
project(
  rc
  VERSION 1.0
  DESCRIPTION "rust like Rc for c++"
  LANGUAGES CXX)

if(PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

add_library(rstd.rc)
add_library(rstd::rc ALIAS rstd.rc)
target_compile_features(rstd.rc PUBLIC cxx_std_20)
target_sources(
  rstd.rc
  PUBLIC FILE_SET
         all
         TYPE
         CXX_MODULES
         BASE_DIRS
         src
         FILES
         src/rc.cppm
         src/mod.cppm)

# Add installation rules
if(PROJECT_IS_TOP_LEVEL)
  include(GNUInstallDirs)
  install(
    TARGETS rstd.rc
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

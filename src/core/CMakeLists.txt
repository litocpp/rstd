cmake_minimum_required(VERSION 3.28)
project(
  rstd.core
  VERSION 0.1.0
  LANGUAGES CXX)

add_library(rstd.core)
add_library(rstd::core ALIAS rstd.core)
target_compile_features(rstd.core PUBLIC cxx_std_20)
target_include_directories(rstd.core PUBLIC include)
target_sources(
  rstd.core
  PUBLIC FILE_SET
         headers
         TYPE
         HEADERS
         BASE_DIRS
         include
         FILES
         include/rstd/trait_macro.hpp)
target_sources(
  rstd.core
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         core.cppm
         trait.cppm)

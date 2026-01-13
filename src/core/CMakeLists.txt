cmake_minimum_required(VERSION 3.28)
project(
  rstd.core
  VERSION 0.1.0
  LANGUAGES CXX)

add_library(rstd.core)
add_library(rstd::core ALIAS rstd.core)
target_compile_features(rstd.core PUBLIC cxx_std_20)
target_include_directories(rstd.core PUBLIC include)
target_compile_options(
  rstd.core PUBLIC "$<$<COMPILE_LANG_AND_ID:CXX,Clang>:-fsized-deallocation>")
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

target_sources(rstd.core PRIVATE core.cpp)
target_sources(
  rstd.core
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         core.cppm
         meta.cppm
         trait.cppm
         marker.cppm
         convert.cppm
         clone.cppm
         option.cppm
         result.cppm
         fmt.cppm
         panic.cppm
         log.cppm
         cppstd.cppm
         enum.cppm
         pin.cppm
         hint.cppm
         assert.cppm
         intrinsics.cppm
         tuple.cppm
         ffi/mod.cppm
         ffi/c_str.cppm
         hash/mod.cppm
         num/mod.cppm
         num/nonzero.cppm
         num/niche_types.cppm
         num/integer.cppm
         slice/mod.cppm
         str/mod.cppm
         ptr/mod.cppm
         ptr/non_null.cppm
         ptr/unique.cppm
         ops/mod.cppm
         ops/function.cppm
         mem/mod.cppm
         mem/manually_drop.cppm
         mem/maybe_uninit.cppm
         sync/mod.cppm
         sync/atomic.cppm
         forward/mod.cppm
         )

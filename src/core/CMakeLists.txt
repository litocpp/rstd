cmake_minimum_required(VERSION 3.28)
project(
  rstd.core
  VERSION 0.1.0
  LANGUAGES CXX)

add_library(rstd.core STATIC)
add_library(rstd::core ALIAS rstd.core)
target_compile_features(rstd.core PUBLIC cxx_std_20)
target_include_directories(
  rstd.core
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>)
target_link_libraries(rstd.core PUBLIC rstd.basic PRIVATE rstd.runtime)
target_compile_definitions(
  rstd.core
  PRIVATE
    "RSTD_CHECK_INTEGER_OVERFLOW=$<OR:$<CONFIG:Debug>,$<BOOL:${RSTD_CHECK_INTEGER_OVERFLOW}>>")
target_sources(
  rstd.core
  PUBLIC FILE_SET
         headers
         TYPE
         HEADERS
         BASE_DIRS
         include
         FILES
         include/rstd/enum.hpp
         include/rstd/macro.hpp)

target_sources(rstd.core PRIVATE src/lib.cpp)
target_sources(
  rstd.core
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/lib.cppm
         src/prelude.cppm
         src/core.cppm
         src/array.cppm
         src/type_id.cppm
         src/any.cppm
         src/error/trait.cppm
         src/error/mod.cppm
         src/trait.cppm
         src/marker.cppm
         src/borrow.cppm
         src/convert.cppm
         src/clone.cppm
         src/clone_tuple.cppm
         src/option.cppm
         src/result.cppm
         src/try_.cppm
         src/enum_.cppm
         src/task.cppm
         src/future.cppm
         src/fmt/mod.cppm
         src/fmt/floating.cppm
         src/panic.cppm
         src/panicking.cppm
         src/choice.cppm
         src/hint.cppm
         src/memchr.cppm
         src/cmp.cppm
         src/time.cppm
         src/alloc/global.cppm
         src/alloc/layout.cppm
         src/alloc/mod.cppm
         src/intrinsics.cppm
         src/ffi/mod.cppm
         src/ffi/c_str.cppm
         src/hash.cppm
         src/num/types.cppm
         src/num/literals.cppm
         src/num/integer_methods.cppm
         src/num/convert.cppm
         src/num/mod.cppm
         src/num/bignum.cppm
         src/num/dec2flt.cppm
         src/num/flt2dec.cppm
         src/num/nonzero.cppm
         src/num/niche_types.cppm
         src/num/integer.cppm
         src/num/floating_parse.cppm
         src/slice/mod.cppm
         src/slice/ops.cppm
         src/str/mod.cppm
         src/str/str.cppm
         src/str/traits.cppm
         src/ptr/mod.cppm
         src/ptr/non_null.cppm
         src/ptr/dyn.cppm
         src/ptr/metadata.cppm
         src/ptr/ptr.cppm
         src/ops/mod.cppm
         src/ops/control_flow.cppm
         src/ops/deref.cppm
         src/ops/function.cppm
         src/iter/mod.cppm
         src/iter/traits.cppm
         src/iter/adapters.cppm
         src/iter/sources.cppm
         src/iter/range.cppm
         src/iter/option_result.cppm
         src/mem/mod.cppm
         src/mem/manually_drop.cppm
         src/mem/maybe_uninit.cppm
         src/mem/drop_guard.cppm
         src/sync/mod.cppm
         src/sync/atomic.cppm
         src/forward.cppm
         src/char_.cppm
         src/ascii.cppm)

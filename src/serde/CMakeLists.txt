add_library(rstd.serde STATIC)
add_library(rstd::serde ALIAS rstd.serde)
target_compile_features(rstd.serde PUBLIC cxx_std_20)
target_link_libraries(rstd.serde PUBLIC rstd.alloc)

target_sources(rstd.serde PRIVATE src/error.cpp)

target_sources(
  rstd.serde
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/lib.cppm
         src/error.cppm
         src/traits.cppm
         src/primitives.cppm
         src/containers.cppm
         src/tuple.cppm
         src/record.cppm
         src/token.cppm)

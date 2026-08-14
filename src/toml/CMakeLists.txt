add_library(rstd.toml STATIC)
add_library(rstd::toml ALIAS rstd.toml)
target_compile_features(rstd.toml PUBLIC cxx_std_20)
target_link_libraries(rstd.toml PUBLIC rstd.core rstd.alloc)

target_sources(
  rstd.toml
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/lib.cppm
         src/datetime.cppm
         src/error.cppm
         src/value.cppm
         src/parser.cppm
         src/serialize.cppm)

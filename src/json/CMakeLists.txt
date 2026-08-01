add_library(rstd.json STATIC)
add_library(rstd::json ALIAS rstd.json)
target_compile_features(rstd.json PUBLIC cxx_std_20)
target_link_libraries(rstd.json PUBLIC rstd.core rstd.std)

target_sources(
  rstd.json
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/lib.cppm
         src/number.cppm
         src/error.cppm
         src/value.cppm
         src/parser.cppm
         src/serialize.cppm)

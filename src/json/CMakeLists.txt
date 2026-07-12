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
         mod.cppm
         number.cppm
         error.cppm
         value.cppm
         parser.cppm
         serialize.cppm)

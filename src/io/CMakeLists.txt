add_library(rstd.io)
add_library(rstd::io ALIAS rstd.io)
target_compile_features(rstd.io PUBLIC cxx_std_23)
target_link_libraries(rstd.io PUBLIC rstd.core)
target_sources(
  rstd.io
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         error.cppm
         )

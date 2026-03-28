add_library(rstd.basic STATIC)
target_compile_features(rstd.basic PUBLIC cxx_std_20)
target_sources(
  rstd.basic
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         prelude.cppm
         basic.cppm
         mtp.cppm
         mtp.std.cppm)

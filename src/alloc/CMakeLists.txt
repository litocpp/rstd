add_library(rstd.alloc)
add_library(rstd::alloc ALIAS rstd.sys)
target_compile_features(rstd.alloc PUBLIC cxx_std_20)
target_link_libraries(rstd.alloc PUBLIC rstd.core)
target_sources(
  rstd.alloc
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         rc.cppm
         string.cppm
         str.cppm
         boxed.cppm
         sync.cppm
         )

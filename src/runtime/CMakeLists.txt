add_library(rstd.runtime STATIC)
target_compile_features(rstd.runtime PUBLIC cxx_std_20)
target_link_libraries(rstd.runtime PUBLIC rstd.basic)
target_sources(rstd.runtime PRIVATE mod.cpp)
target_sources(
  rstd.runtime
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm)

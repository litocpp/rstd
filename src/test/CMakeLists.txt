add_library(rstd.test STATIC)
add_library(rstd::test ALIAS rstd.test)
target_compile_features(rstd.test PUBLIC cxx_std_20)
target_link_libraries(rstd.test PUBLIC rstd.std)

target_sources(
  rstd.test
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/lib.cppm
         src/model.cppm
         src/runner.cppm)

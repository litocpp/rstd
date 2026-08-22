add_library(rstd.parse STATIC)
add_library(rstd::parse ALIAS rstd.parse)
target_compile_features(rstd.parse PUBLIC cxx_std_20)
target_link_libraries(rstd.parse PUBLIC rstd.alloc)

target_sources(rstd.parse PRIVATE src/source.cpp src/error.cpp)

target_sources(
  rstd.parse
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/lib.cppm
         src/source.cppm
         src/error.cppm
         src/rule.cppm)

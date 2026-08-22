add_library(rstd.parse STATIC)
add_library(rstd::parse ALIAS rstd.parse)
target_compile_features(rstd.parse PUBLIC cxx_std_20)
target_link_libraries(rstd.parse PUBLIC rstd.core rstd.alloc)

target_sources(rstd.parse PRIVATE src/alloc/mod.cpp)

target_sources(
  rstd.parse
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/lib.cppm
         src/core/mod.cppm
         src/core/source.cppm
         src/core/predicate.cppm
         src/core/consume.cppm
         src/core/collection.cppm
         src/core/rule.cppm
         src/alloc/mod.cppm
         src/alloc/source.cppm
         src/alloc/error.cppm
         src/alloc/rule.cppm
         src/regex/syntax.cppm
         src/regex/program.cppm
         src/regex/mod.cppm)

add_library(rstd.argparse STATIC)
add_library(rstd::argparse ALIAS rstd.argparse)
target_compile_features(rstd.argparse PUBLIC cxx_std_20)
target_link_libraries(rstd.argparse PUBLIC rstd.std)

target_sources(
  rstd.argparse
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         src/lib.cppm
         src/error.cppm
         src/value_parser.cppm
         src/arg.cppm
         src/command_key.cppm
         src/schema.cppm
         src/matches.cppm
         src/parser.cppm
         src/command.cppm)

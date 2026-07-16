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
         mod.cppm
         error.cppm
         value_parser.cppm
         arg.cppm
         schema.cppm
         matches.cppm
         parser.cppm
         command.cppm)

add_library(rstd.thread)
add_library(rstd::thread ALIAS rstd.thread)
target_compile_features(rstd.thread PUBLIC cxx_std_20)
target_link_libraries(rstd.thread PUBLIC rstd.core)
target_sources(
  rstd.thread
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         id.cppm
         #thread.cppm
         )

add_library(rstd.thread)
add_library(rstd::thread ALIAS rstd.thread)
target_compile_features(rstd.thread PUBLIC cxx_std_23)
target_link_libraries(rstd.thread PUBLIC rstd.core rstd.alloc rstd.sys)
target_sources(
  rstd.thread
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         id.cppm
         thread.cppm
         main_thread.cppm
         builder.cppm
         functions.cppm
         join_handle.cppm
         lifecycle.cppm
         scoped.cppm
         )

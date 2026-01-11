add_library(rstd.sync)
add_library(rstd::sync ALIAS rstd.sync)
target_compile_features(rstd.sync PUBLIC cxx_std_20)
target_link_libraries(rstd.sync PUBLIC rstd.alloc)
target_sources(
  rstd.sync
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         poison/mod.cppm
         poison/once.cppm
         # mpsc/mod.cppm mpsc/mpmc/mod.cppm mpsc/mpmc/counter.cppm
         # mpsc/mpmc/array.cppm mpsc/mpmc/utils.cppm mpsc/mpmc/context.cppm
)

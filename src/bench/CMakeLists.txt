add_library(rstd.bench STATIC)
add_library(rstd::bench ALIAS rstd.bench)
target_compile_features(rstd.bench PUBLIC cxx_std_20)
target_link_libraries(rstd.bench PUBLIC rstd.std)

target_sources(
  rstd.bench
  PUBLIC FILE_SET
         modules
         TYPE
         CXX_MODULES
         FILES
         mod.cppm
         clock.cppm
         model.cppm
         statistics.cppm
         counters.cppm
         benchmark.cppm
         probe/mod.cppm
         probe/model.cppm
         probe/registry.cppm
         probe/recorder.cppm
         probe/collector.cppm
         probe/report.cppm)

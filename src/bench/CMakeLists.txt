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
         src/lib.cppm
         src/clock.cppm
         src/model.cppm
         src/statistics.cppm
         src/counters.cppm
         src/benchmark.cppm
         src/probe/mod.cppm
         src/probe/model.cppm
         src/probe/registry.cppm
         src/probe/recorder.cppm
         src/probe/collector.cppm
         src/probe/report.cppm)

add_executable(
  rstd_bench
  bench_main.cpp
  alloc.cpp
  sync.cpp
  async.cpp
  net.cpp
  benchmark.cpp)

target_sources(
  rstd_bench
  PRIVATE FILE_SET modules TYPE CXX_MODULES FILES async_bench_tools.cppm benchmark.cppm)

target_link_libraries(rstd_bench PRIVATE rstd::rstd rstd.cppstd rstd::bench rstd::json)

if(CMAKE_BUILD_TYPE)
  set(RSTD_BENCH_BUILD_TYPE_VALUE "${CMAKE_BUILD_TYPE}")
else()
  set(RSTD_BENCH_BUILD_TYPE_VALUE "unknown")
endif()

target_compile_definitions(
  rstd_bench PRIVATE RSTD_BENCH_BUILD_TYPE="${RSTD_BENCH_BUILD_TYPE_VALUE}"
                     RSTD_BENCH_ASAN=$<BOOL:${RSTD_USE_ASAN}>)

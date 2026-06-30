add_executable(
  rstd_bench
  bench_main.cpp
  alloc.cpp
  sync.cpp
  async.cpp
  net.cpp)

target_link_libraries(rstd_bench PRIVATE rstd::rstd)

if(CMAKE_BUILD_TYPE)
  set(RSTD_BENCH_BUILD_TYPE_VALUE "${CMAKE_BUILD_TYPE}")
else()
  set(RSTD_BENCH_BUILD_TYPE_VALUE "unknown")
endif()

target_compile_definitions(
  rstd_bench PRIVATE RSTD_BENCH_BUILD_TYPE="${RSTD_BENCH_BUILD_TYPE_VALUE}"
                     RSTD_BENCH_ASAN=$<BOOL:${RSTD_USE_ASAN}>)

if(RSTD_BENCHMARKS_IN_CTEST)
  add_test(NAME rstd_bench_quick COMMAND rstd_bench --suite all --quick)
  set_tests_properties(rstd_bench_quick PROPERTIES LABELS benchmark)
endif()

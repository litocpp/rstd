#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

import rstd.bench;
import rstd.cppstd;

namespace rstd_bench
{

struct CaseRunResult {
    rstd::Result<rstd::bench::BenchmarkResult, rstd::bench::BenchError> measurement;
    bool                                                                ok;
};

using BenchFn = CaseRunResult (*)(rstd::bench::BenchConfig);

struct BenchCase {
    const char*   m_suite;
    const char*   m_name;
    std::uint64_t m_quick_iterations;
    BenchFn       m_run;
};

struct BenchList {
    const BenchCase* m_cases;
    std::size_t      m_len;
};

template<typename Operation, typename Validate>
auto measure_case(const char*              name,
                  rstd::bench::BenchConfig config,
                  rstd::bench::RunConfig   run_config,
                  Operation&&              operation,
                  Validate&&               validate) -> CaseRunResult {
    auto runner         = rstd::bench::Bench::new_(rstd::move(config));
    auto benchmark_name = rstd::cppstd::as_str(std::string_view(name)).unwrap_unchecked();
    auto measurement =
        runner.run(benchmark_name, rstd::forward<Operation>(operation), rstd::move(run_config));
    auto const valid = rstd::forward<Validate>(validate)();
    return { rstd::move(measurement), valid };
}

BenchList alloc_benchmarks();
BenchList sync_benchmarks();
BenchList async_benchmarks();
BenchList net_benchmarks();

} // namespace rstd_bench

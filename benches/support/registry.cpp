module rstd_benches;
import rstd;

using namespace rstd::prelude;

auto rstd_bench::registry() -> Vec<BenchCase> {
    const BenchList groups[] = { alloc_benchmarks(),         iter_benchmarks(),
                                 slice_benchmarks(),         sync_benchmarks(),
                                 async_runtime_benchmarks(), async_loopback_benchmarks(),
                                 async_io_benchmarks(),      net_benchmarks() };
    auto            cases    = Vec<BenchCase>::make();
    for (const auto& group : groups)
        for (rstd::size_t index = 0; index < group.m_len; ++index)
            cases.emplace_back(group.m_cases[index]);
    return cases;
}

export module rstd.benchmark;
export import rstd.bench;
export import rstd.cppstd;

export namespace rstd_bench
{

struct CaseRunResult {
    rstd::Result<rstd::bench::BenchmarkResult, rstd::bench::BenchError> measurement;
    bool                                                                ok;
};

using BenchFn = CaseRunResult (*)(rstd::bench::BenchConfig, const char*);

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

} // namespace rstd_bench

namespace rstd_bench
{

using OperationFn = void (*)(void*);
using ValidateFn  = bool (*)(void*);

auto measure_case_erased(const char*               name,
                         rstd::bench::BenchConfig* config,
                         rstd::bench::RunConfig*   run_config,
                         void*                     operation,
                         OperationFn               operation_fn,
                         void*                     validate,
                         ValidateFn                validate_fn) -> CaseRunResult;

template<typename Operation>
void invoke_operation(void* context) {
    (*static_cast<Operation*>(context))();
}

template<typename Validate>
auto invoke_validate(void* context) -> bool {
    return (*static_cast<Validate*>(context))();
}

} // namespace rstd_bench

export namespace rstd_bench
{

template<typename Operation, typename Validate>
auto measure_case(const char*              name,
                  rstd::bench::BenchConfig config,
                  rstd::bench::RunConfig   run_config,
                  Operation                operation,
                  Validate                 validate) -> CaseRunResult {
    return measure_case_erased(name,
                               &config,
                               &run_config,
                               &operation,
                               &invoke_operation<Operation>,
                               &validate,
                               &invoke_validate<Validate>);
}

auto alloc_benchmarks() -> BenchList;
auto sync_benchmarks() -> BenchList;
auto async_benchmarks() -> BenchList;
auto net_benchmarks() -> BenchList;

} // namespace rstd_bench

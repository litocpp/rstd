module rstd.benchmark;

namespace rstd_bench
{

struct ErasedOperation {
    void*       context;
    OperationFn invoke;

    void operator()() const { invoke(context); }
};

auto measure_case_erased(const char*               name,
                         rstd::bench::BenchConfig* config,
                         rstd::bench::RunConfig*   run_config,
                         void*                     operation,
                         OperationFn               operation_fn,
                         void*                     validate,
                         ValidateFn                validate_fn) -> CaseRunResult {
    auto runner         = rstd::bench::Bench::new_(rstd::move(*config));
    auto benchmark_name = rstd::ffi::CStr::from_ptr(name).to_str().unwrap_unchecked();
    auto measurement    = runner.run(
        benchmark_name, ErasedOperation { operation, operation_fn }, rstd::move(*run_config));
    return { rstd::move(measurement), validate_fn(validate) };
}

} // namespace rstd_bench

module;
#include <rstd/enum.hpp>

export module rstd_benches;
import rstd;
import rstd.bench;
import rstd.json;

using namespace rstd::prelude;

export namespace rstd_bench
{

class CaseRunResult {
    RSTD_ENUM(CaseRunResult,
              (Passed, (rstd::bench::BenchmarkResult measurement;)),
              (Failed, (Vec<String> errors;)),
              (Skipped, (String reason;)))
};

using BenchFn = CaseRunResult (*)(rstd::bench::BenchConfig, const char*);

struct Parameters {
    rstd::uint64_t n {};
    rstd::uint64_t keys {};
    rstd::uint64_t threads {};
    const char*    distribution { "unknown" };
};

struct BenchCase {
    const char*    m_suite;
    const char*    m_name;
    rstd::uint64_t m_quick_iterations;
    BenchFn        m_run;
    Parameters     parameters {};
};

struct BenchList {
    const BenchCase* m_cases;
    rstd::size_t     m_len;
};

auto text(const char* value) -> ref<str>;
auto failed(String reason, Result<empty, String> cleanup = Ok(empty {})) -> CaseRunResult;
auto complete_measurement(Result<rstd::bench::BenchmarkResult, rstd::bench::BenchError> measurement,
                          bool                                                          valid,
                          Result<empty, String> cleanup = Ok(empty {})) -> CaseRunResult;
auto complete_measurement(Result<rstd::bench::BenchmarkResult, rstd::bench::BenchError> measurement,
                          Result<empty, String>                                         validation,
                          Result<empty, String> cleanup = Ok(empty {})) -> CaseRunResult;

template<typename Operation, typename Validate, typename Cleanup>
auto measure_case(const char*              name,
                  rstd::bench::BenchConfig config,
                  rstd::bench::RunConfig   run_config,
                  Operation                operation,
                  Validate                 validate,
                  Cleanup                  cleanup) -> CaseRunResult {
    auto runner      = rstd::bench::Bench::new_(rstd::move(config));
    auto measurement = runner.run(text(name), operation, rstd::move(run_config));
    auto cleaned     = cleanup();
    auto valid       = validate();
    return complete_measurement(rstd::move(measurement), rstd::move(valid), rstd::move(cleaned));
}

template<typename Operation, typename Validate>
auto measure_case(const char*              name,
                  rstd::bench::BenchConfig config,
                  rstd::bench::RunConfig   run_config,
                  Operation                operation,
                  Validate                 validate) -> CaseRunResult {
    return measure_case(name,
                        rstd::move(config),
                        rstd::move(run_config),
                        rstd::move(operation),
                        rstd::move(validate),
                        []() -> Result<empty, String> {
                            return Ok(empty {});
                        });
}

auto alloc_benchmarks() -> BenchList;
auto iter_benchmarks() -> BenchList;
auto slice_benchmarks() -> BenchList;
auto sync_benchmarks() -> BenchList;
auto async_runtime_benchmarks() -> BenchList;
auto async_loopback_benchmarks() -> BenchList;
auto async_io_benchmarks() -> BenchList;
auto net_benchmarks() -> BenchList;
auto registry() -> Vec<BenchCase>;

struct Options {
    String                      suite;
    Option<String>              name;
    Option<rstd::path::PathBuf> json_path;
    Option<u64>                 iterations;
    bool                        quick {};
    bool                        list {};
    Option<String>              display;
};

struct RunResult {
    BenchCase     benchmark;
    CaseRunResult outcome;
};

auto parse_options(Vec<rstd::ffi::OsString> arguments) -> Result<Options, String>;
auto select_cases(const Options& options, slice<BenchCase> cases) -> Result<Vec<BenchCase>, String>;
auto run_case(const Options& options, const BenchCase& benchmark) -> RunResult;
auto print_result(const RunResult& result) -> void;
auto report(const Options& options, const Vec<RunResult>& results) -> rstd::json::Value;
auto execute(const Options& options) -> Result<bool, String>;

} // namespace rstd_bench

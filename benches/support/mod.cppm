module;
#include <rstd/enum.hpp>

export module rstd_benches;
import rstd;
import rstd.bench;
import rstd.bench.json;
import rstd.json;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace rstd_bench
{

namespace report  = rstd::bench::report;
namespace console = rstd::bench::console;
using rstd::bench::ConsoleOptions;
using rstd::bench::ConsoleRenderer;
using rstd::bench::ConsoleReporter;

using CaseRunResult = rstd::bench::CaseOutcome;
using rstd::bench::failed;
using rstd::bench::complete_measurement;

inline auto parameter(ref<str> name, u64 value) -> rstd::bench::Parameter {
    return { String::make(name), rstd::bench::ParameterValue::Unsigned(value) };
}
inline auto parameter(ref<str> name, ref<str> value) -> rstd::bench::Parameter {
    return { String::make(name), rstd::bench::ParameterValue::Text(String::make(value)) };
}
template<typename... P>
auto make_descriptor(ref<str> suite, ref<str> name, u64 quick, P... parameters)
    -> rstd::bench::CaseDescriptor {
    rstd::bench::CaseDescriptor result { rstd::format("{}.{}", suite, name),
                                         String::make(suite),
                                         String::make("1"_str),
                                         String::make("rstd"_str) };
    result.name             = String::make(name);
    result.quick_iterations = quick;
    (result.parameters.push(rstd::move(parameters)), ...);
    return result;
}

auto text(const char* value) -> ref<str>;
auto complete_measurement(Result<rstd::bench::BenchmarkResult, rstd::bench::BenchError> measurement,
                          bool                                                          valid,
                          Result<empty, String> cleanup = Ok(empty {})) -> CaseRunResult;

template<typename Operation, typename Validate, typename Cleanup>
auto measure_case(ref<str>               name,
                  rstd::bench::Bench&    runner,
                  rstd::bench::RunConfig run_config,
                  Operation              operation,
                  Validate               validate,
                  Cleanup                cleanup) -> CaseRunResult {
    auto measurement = runner.run(name, operation, rstd::move(run_config));
    auto cleaned     = cleanup();
    auto valid       = validate();
    return complete_measurement(rstd::move(measurement), rstd::move(valid), rstd::move(cleaned));
}

template<typename Operation, typename Validate>
auto measure_case(ref<str>               name,
                  rstd::bench::Bench&    runner,
                  rstd::bench::RunConfig run_config,
                  Operation              operation,
                  Validate               validate) -> CaseRunResult {
    return measure_case(name,
                        runner,
                        rstd::move(run_config),
                        rstd::move(operation),
                        rstd::move(validate),
                        []() -> Result<empty, String> {
                            return Ok(empty {});
                        });
}

auto register_alloc(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_iter(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_slice(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_simd_operations(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_simd(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_random(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_sync(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_async_runtime(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_async_loopback(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_async_io(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_net(rstd::bench::Suite& suite) -> Result<empty, String>;
auto add_domain_cases(rstd::bench::Suite& suite) -> Result<empty, String>;
auto add_collection_cases(rstd::bench::Suite& suite) -> Result<empty, String>;

struct Options {
    String                      suite;
    Option<String>              name;
    Option<rstd::path::PathBuf> json_path;
    Option<u64>                 iterations;
    Option<usize>               epochs;
    Option<u64>                 warmup;
    u64                         repeats { u64(1) };
    rstd::bench::CounterMode    counters { rstd::bench::CounterMode::Disabled() };
    Option<rstd::path::PathBuf> baseline;
    Option<rstd::path::PathBuf> candidate;
    bool                        cross_environment {};
    bool                        stop_on_failure {};
    bool                        quick {};
    bool                        list {};
    bool                        diagnose {};
    bool                        verbose {};
    Option<String>              display;
};

auto parse_options(Vec<rstd::ffi::OsString> arguments) -> Result<Options, String>;
auto make_suite() -> Result<rstd::bench::Suite, String>;
auto execute(const Options& options) -> Result<bool, String>;

} // namespace rstd_bench

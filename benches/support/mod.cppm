module;
#include <rstd/enum.hpp>

export module rstd_benches;
import rstd;
import rstd.bench;
import rstd.json;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace rstd_bench
{

namespace report
{
auto to_value(const rstd::bench::RunReport& report) -> Result<rstd::json::Value, String>;
auto from_value(const rstd::json::Value& value) -> Result<rstd::bench::RunReport, String>;
auto encode(const rstd::bench::RunReport& report) -> Result<String, String>;
auto decode(ref<str> input) -> Result<rstd::bench::RunReport, String>;
auto read(ref<rstd::path::Path> path) -> Result<rstd::bench::RunReport, String>;
} // namespace report

namespace console
{
auto number(f64 value) -> String;
auto time(f64 ns) -> String;
auto bytes(f64 value) -> String;
auto byte_rate(f64 value) -> String;
auto item_rate(f64 value) -> String;
auto escaped(ref<str> value) -> String;
auto exact(const rstd::bench::DiagnosticValue& value) -> String;
auto context(bool                               quick,
             bool                               diagnostic,
             u64                                repeats,
             bool                               verbose,
             const rstd::bench::RunEnvironment& environment) -> String;
auto comparison(const rstd::bench::ComparisonReport& report, bool verbose) -> String;
auto write(ref<str> text) -> Result<empty, String>;
auto write_comparison(const rstd::bench::ComparisonReport& report, bool verbose)
    -> Result<bool, String>;
} // namespace console

struct ConsoleOptions {
    bool verbose {};
    u64  repeats { u64(1) };
};

class ConsoleRenderer {
    ConsoleOptions options_;
    usize          name_width_ { usize(4) };
    int            table_ {};
    bool           items_ {};
    bool           bytes_ {};

public:
    explicit ConsoleRenderer(ConsoleOptions options = {}): options_(options) {}
    void include(ref<str> name);
    auto render(const rstd::bench::RunResult& result) -> String;
};

template<typename Writer>
struct ConsoleReporter {
    Writer&         writer;
    ConsoleRenderer renderer;
    auto            report(const rstd::bench::RunResult& result) -> Result<empty, String> {
        auto text    = renderer.render(result);
        auto written = rstd::io::write_all(writer, text.as_str().as_bytes());
        if (written.is_err())
            return Err(rstd::format("cannot write benchmark output: {}", written.unwrap_err()));
        return Ok(empty {});
    }
};

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
auto register_sync(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_async_runtime(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_async_loopback(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_async_io(rstd::bench::Suite& suite) -> Result<empty, String>;
auto register_net(rstd::bench::Suite& suite) -> Result<empty, String>;
auto add_domain_cases(rstd::bench::Suite& suite) -> Result<empty, String>;

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

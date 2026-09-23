module rstd_benches;
import rstd;
import rstd.bench;
import rstd.argparse;
import rstd.json;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;

auto bench_error(const rstd::bench::BenchError& error) -> String {
    if (error.is_CounterUnavailable())
        return rstd::format("performance counter unavailable: {}",
                            error.as_CounterUnavailable().code);
    if (error.is_IterationOverflow()) return "iteration or elapsed-time overflow"_Str;
    if (error.is_OperationOptimizedAway()) return "clock did not resolve operation duration"_Str;
    if (error.is_Clock()) return "benchmark clock failed"_Str;
    const auto& reason = error.as_InvalidConfig().reason;
    if (reason.is_ZeroEpochs()) return "epochs must be nonzero"_Str;
    if (reason.is_ZeroMinIterations()) return "iterations must be nonzero"_Str;
    if (reason.is_ZeroClockResolutionMultiple())
        return "clock resolution multiple must be nonzero"_Str;
    if (reason.is_ZeroMaxEpochTime()) return "maximum epoch duration must be nonzero"_Str;
    if (reason.is_InvalidEpochRange()) return "invalid epoch duration range"_Str;
    return "invalid batch configuration"_Str;
}

auto rstd_bench::text(const char* value) -> ref<str> {
    return rstd::ffi::CStr::from_ptr(value).to_str().unwrap();
}

auto rstd_bench::failed(String reason, Result<empty, String> cleanup) -> CaseRunResult {
    auto errors = Vec<String>::make();
    errors.push(rstd::move(reason));
    if (cleanup.is_err()) errors.push(rstd::move(cleanup).unwrap_err());
    return CaseRunResult::Failed(rstd::move(errors));
}

auto rstd_bench::complete_measurement(
    Result<rstd::bench::BenchmarkResult, rstd::bench::BenchError> measurement,
    bool                                                          valid,
    Result<empty, String>                                         cleanup) -> CaseRunResult {
    Result<empty, String> validation = valid ? Result<empty, String>(Ok(empty {}))
                                             : Result<empty, String>(Err("validation failed"_Str));
    return complete_measurement(
        rstd::move(measurement), rstd::move(validation), rstd::move(cleanup));
}

auto rstd_bench::complete_measurement(
    Result<rstd::bench::BenchmarkResult, rstd::bench::BenchError> measurement,
    Result<empty, String>                                         validation,
    Result<empty, String>                                         cleanup) -> CaseRunResult {
    auto errors = Vec<String>::make();
    if (measurement.is_err()) errors.push(bench_error(measurement.unwrap_err()));
    if (validation.is_err()) errors.push(rstd::move(validation).unwrap_err());
    if (cleanup.is_err()) errors.push(rstd::move(cleanup).unwrap_err());
    if (! errors.is_empty()) return CaseRunResult::Failed(rstd::move(errors));
    return CaseRunResult::Passed(rstd::move(measurement).unwrap());
}

auto rstd_bench::parse_options(Vec<rstd::ffi::OsString> arguments) -> Result<Options, String> {
    using namespace rstd::argparse;
    auto command = Command::make("rstd_bench"_str);
    auto suite   = command.add_arg(Arg<String>::value("suite"_str, string_parser())
                                       .long_name("suite"_str)
                                       .default_value("all"_str));
    auto name =
        command.add_arg(Arg<String>::value("case"_str, string_parser()).long_name("case"_str));
    auto iterations = command.add_arg(
        Arg<u64>::value("iterations"_str, from_str_parser<u64>()).long_name("iterations"_str));
    auto json = command.add_arg(
        Arg<rstd::ffi::OsString>::value("json"_str, os_string_parser()).long_name("json"_str));
    auto quick  = command.add_arg(Arg<bool>::flag("quick"_str).long_name("quick"_str));
    auto list   = command.add_arg(Arg<bool>::flag("list"_str).long_name("list"_str));
    auto parser = rstd::move(command).build().unwrap();
    auto parsed = parser.parse_from(rstd::move(arguments));
    if (parsed.is_err()) return Err(rstd::format("{}", parsed.unwrap_err()));
    auto outcome = rstd::move(parsed).unwrap();
    auto result  = Options {};
    if (outcome.is_Display()) {
        result.display = Some(String::make(outcome.as_Display().request.text()));
        return Ok(rstd::move(result));
    }
    auto matches = rstd::move(outcome).as_Parsed().value;
    result.suite = (**matches.get_one(suite).unwrap()).clone();
    if (auto value = matches.get_one(name).unwrap(); value.is_some())
        result.name = Some((**value).clone());
    if (auto value = matches.get_one(iterations).unwrap(); value.is_some()) {
        if (**value == u64()) return Err("iterations must be nonzero"_Str);
        result.iterations = Some(u64(**value));
    }
    if (auto value = matches.get_one(json).unwrap(); value.is_some())
        result.json_path = Some(rstd::path::PathBuf::from((**value).as_os_str()));
    if (auto value = matches.get_one(quick).unwrap(); value.is_some()) result.quick = **value;
    if (auto value = matches.get_one(list).unwrap(); value.is_some()) result.list = **value;
    return Ok(rstd::move(result));
}

auto rstd_bench::select_cases(const Options& options, slice<BenchCase> cases)
    -> Result<Vec<BenchCase>, String> {
    auto selected = Vec<BenchCase>::make();
    for (const auto& benchmark : cases) {
        if (options.suite != "all"_str && options.suite != text(benchmark.m_suite)) continue;
        if (options.name.is_some() && *options.name != text(benchmark.m_name)) continue;
        selected.emplace_back(benchmark);
    }
    if (selected.is_empty()) return Err("no benchmark cases selected"_Str);
    return Ok(rstd::move(selected));
}

auto rstd_bench::run_case(const Options& options, const BenchCase& benchmark) -> RunResult {
    auto config = rstd::bench::BenchConfig {};
    if (options.quick) {
        config.epochs                 = usize(3);
        config.exact_epoch_iterations = Some(u64(benchmark.m_quick_iterations));
        config.warmup_iterations      = u64(1);
        config.counter_mode           = rstd::bench::CounterMode::Disabled();
    }
    if (options.iterations.is_some()) config.exact_epoch_iterations = options.iterations;
    return { benchmark, benchmark.m_run(rstd::move(config), benchmark.m_name) };
}

auto rstd_bench::execute(const Options& options) -> Result<bool, String> {
    if (options.display.is_some()) {
        rstd::io::print("{}", options.display->as_str());
        return Ok(true);
    }
    auto cases     = registry();
    auto selection = select_cases(options, cases.as_slice());
    if (selection.is_err()) return Err(rstd::move(selection).unwrap_err());
    if (options.list) {
        for (const auto& benchmark : *selection)
            rstd::io::println("{}.{}", text(benchmark.m_suite), text(benchmark.m_name));
        return Ok(true);
    }
    rstd::io::println("mode={}; timing=wall; profile=unknown",
                      options.quick ? "quick (smoke only)"_str : "normal"_str);
    auto results = Vec<RunResult>::make();
    auto ok      = true;
    for (const auto& benchmark : *selection) {
        auto result = run_case(options, benchmark);
        if (result.outcome.is_Failed()) ok = false;
        print_result(result);
        results.push(rstd::move(result));
    }
    if (options.json_path.is_some()) {
        auto json    = rstd::json::to_string(report(options, results),
                                             rstd::json::FormatOptions { .pretty = true });
        auto written = rstd::fs::write(options.json_path->as_path(), json.as_str().as_bytes());
        if (written.is_err())
            return Err(rstd::format("cannot write report: {}", written.unwrap_err()));
    }
    return Ok(ok);
}

module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.bench;
import rstd.argparse;
import rstd.json;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;

auto rstd_bench::text(const char* value) -> ref<str> {
    return rstd::ffi::CStr::from_ptr(value).to_str().unwrap();
}

auto rstd_bench::complete_measurement(
    Result<rstd::bench::BenchmarkResult, rstd::bench::BenchError> measurement,
    bool                                                          valid,
    Result<empty, String>                                         cleanup) -> CaseRunResult {
    Result<empty, String> validation = valid ? Result<empty, String>(Ok(empty {}))
                                             : Result<empty, String>(Err("validation failed"_Str));
    return rstd::bench::complete_measurement(
        rstd::move(measurement), rstd::move(validation), rstd::move(cleanup));
}

auto rstd_bench::parse_options(Vec<rstd::ffi::OsString> arguments) -> Result<Options, String> {
    using namespace rstd::argparse;
    auto command    = Command::make("rstd_bench"_str);
    auto suite      = command.add_arg(Arg<String>::value("suite"_str, string_parser())
                                          .long_name("suite"_str)
                                          .default_value("all"_str)
                                          .help("select a suite"_str));
    auto name       = command.add_arg(Arg<String>::value("case"_str, string_parser())
                                          .long_name("case"_str)
                                          .help("select an exact case name"_str));
    auto iterations = command.add_arg(Arg<u64>::value("iterations"_str, from_str_parser<u64>())
                                          .long_name("iterations"_str)
                                          .help("operations per epoch or diagnostic run"_str));
    auto json  = command.add_arg(Arg<rstd::ffi::OsString>::value("json"_str, os_string_parser())
                                     .long_name("json"_str)
                                     .help("write a JSON report"_str));
    auto quick = command.add_arg(Arg<bool>::flag("quick"_str)
                                     .long_name("quick"_str)
                                     .help("smoke only; not a performance baseline"_str));
    auto list  = command.add_arg(
        Arg<bool>::flag("list"_str).long_name("list"_str).help("list without preparing cases"_str));
    auto diagnose = command.add_arg(Arg<bool>::flag("diagnose"_str)
                                        .long_name("diagnose"_str)
                                        .help("resource diagnostics without timing"_str));
    auto verbose = command.add_arg(Arg<bool>::flag("verbose"_str)
                                       .long_name("verbose"_str)
                                       .help("show sampling, exact values and metric sources"_str));
    auto epochs  = command.add_arg(Arg<usize>::value("epochs"_str, from_str_parser<usize>())
                                       .long_name("epochs"_str)
                                       .help("measured epochs per case"_str));
    auto warmup  = command.add_arg(Arg<u64>::value("warmup"_str, from_str_parser<u64>())
                                       .long_name("warmup"_str)
                                       .help("unmeasured operations before sampling"_str));
    auto repeats = command.add_arg(Arg<u64>::value("repeats"_str, from_str_parser<u64>())
                                       .long_name("repeats"_str)
                                       .help("fresh sessions in the same process"_str));
    auto counters = command.add_arg(Arg<String>::value("counters"_str, string_parser())
                                        .long_name("counters"_str)
                                        .help("disabled (default), auto or required"_str));
    auto baseline =
        command.add_arg(Arg<rstd::ffi::OsString>::value("baseline"_str, os_string_parser())
                            .long_name("baseline"_str)
                            .help("compare against a saved timing report"_str));
    auto candidate =
        command.add_arg(Arg<rstd::ffi::OsString>::value("compare"_str, os_string_parser())
                            .long_name("compare"_str)
                            .help("read a candidate report instead of running cases"_str));
    auto cross = command.add_arg(
        Arg<bool>::flag("cross-environment"_str)
            .long_name("cross-environment"_str)
            .help("allow descriptive comparison with missing or different environment"_str));
    auto stop   = command.add_arg(Arg<bool>::flag("stop-on-failure"_str)
                                      .long_name("stop-on-failure"_str)
                                      .help("stop after the first failed case"_str));
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
    if (auto value = matches.get_one(diagnose).unwrap(); value.is_some()) result.diagnose = **value;
    if (auto value = matches.get_one(verbose).unwrap(); value.is_some()) result.verbose = **value;
    if (auto value = matches.get_one(epochs).unwrap(); value.is_some()) {
        if (**value == usize()) return Err("epochs must be nonzero"_Str);
        result.epochs = Some(usize(**value));
    }
    if (auto value = matches.get_one(warmup).unwrap(); value.is_some())
        result.warmup = Some(u64(**value));
    if (auto value = matches.get_one(repeats).unwrap(); value.is_some()) {
        if (**value == u64()) return Err("repeats must be nonzero"_Str);
        result.repeats = **value;
    }
    if (auto value = matches.get_one(counters).unwrap(); value.is_some()) {
        auto selected = (**value).as_str();
        if (selected == "disabled"_str)
            result.counters = rstd::bench::CounterMode::Disabled();
        else if (selected == "auto"_str)
            result.counters = rstd::bench::CounterMode::Auto();
        else if (selected == "required"_str)
            result.counters = rstd::bench::CounterMode::Required();
        else
            return Err("counters must be disabled, auto or required"_Str);
    }
    if (auto value = matches.get_one(baseline).unwrap(); value.is_some())
        result.baseline = Some(rstd::path::PathBuf::from((**value).as_os_str()));
    if (auto value = matches.get_one(candidate).unwrap(); value.is_some())
        result.candidate = Some(rstd::path::PathBuf::from((**value).as_os_str()));
    if (auto value = matches.get_one(cross).unwrap(); value.is_some())
        result.cross_environment = **value;
    if (auto value = matches.get_one(stop).unwrap(); value.is_some())
        result.stop_on_failure = **value;
    if ((result.candidate.is_some() || result.cross_environment) && result.baseline.is_none())
        return Err("compare and cross-environment require baseline"_Str);
    if (result.baseline.is_some() && (result.quick || result.diagnose || result.list))
        return Err("baseline comparison requires a timing run"_Str);
    if (result.candidate.is_some() &&
        (result.json_path.is_some() || result.iterations.is_some() || result.epochs.is_some() ||
         result.warmup.is_some() || matches.get_one(repeats).unwrap().is_some() ||
         matches.get_one(counters).unwrap().is_some() || result.stop_on_failure))
        return Err("offline comparison does not accept sampling or output options"_Str);
    if ((result.quick || result.diagnose) &&
        (result.epochs.is_some() || result.warmup.is_some() || ! result.counters.is_Disabled()))
        return Err("epochs, warmup and counters require normal timing mode"_Str);
    return Ok(rstd::move(result));
}

auto report_selection(const Options& options) -> rstd::bench::RunnerOptions {
    rstd::bench::RunnerOptions selection;
    if (options.suite != "all"_str) selection.suite = Some(options.suite.clone());
    if (options.name.is_some()) selection.name = Some(options.name->clone());
    return selection;
}

auto select_report(rstd::bench::RunReport input, const rstd::bench::RunnerOptions& selection)
    -> Result<rstd::bench::RunReport, String> {
    input.results.retain([&](const rstd::bench::RunResult& result) {
        return selection.matches(result.descriptor);
    });
    if (input.results.is_empty()) return Err("no report cases selected"_Str);
    return Ok(rstd::move(input));
}

auto rstd_bench::execute(const Options& options) -> Result<bool, String> {
    if (options.display.is_some()) {
        rstd::io::print("{}", options.display->as_str());
        return Ok(true);
    }
    auto                           settings = report_selection(options);
    Option<rstd::bench::RunReport> baseline;
    if (options.baseline.is_some())
        baseline = Some(
            rstd_try(select_report(rstd_try(report::read(options.baseline->as_path())), settings)));
    auto comparison_mode = options.cross_environment ? rstd::bench::ComparisonMode::CrossEnvironment
                                                     : rstd::bench::ComparisonMode::Strict;
    if (options.candidate.is_some()) {
        auto candidate =
            rstd_try(select_report(rstd_try(report::read(options.candidate->as_path())), settings));
        return console::write_comparison(
            rstd::bench::compare(*baseline, candidate, comparison_mode), options.verbose);
    }
    auto created = make_suite();
    if (created.is_err()) return Err(rstd::move(created).unwrap_err());
    auto suite                               = rstd::move(created).unwrap();
    settings.sampling.exact_epoch_iterations = options.iterations;
    if (options.epochs.is_some()) settings.sampling.epochs = *options.epochs;
    if (options.warmup.is_some()) settings.sampling.warmup_iterations = *options.warmup;
    settings.sampling.counter_mode = options.counters;
    settings.repeats               = options.repeats;
    settings.stop_on_failure       = options.stop_on_failure;
    settings.quick                 = options.quick;
    rstd::bench::RunEnvironment environment;
    environment.fields.push(
        { "compiler"_Str, Some(String::make(text(__clang_version__))), "compiler macro"_Str });
    rstd::bench::Runner runner(rstd::move(settings), environment.clone());
    auto                selected = runner.select(suite);
    if (selected.is_err()) return Err(rstd::move(selected).unwrap_err());
    if (options.list) {
        for (auto index : *selected) rstd::io::println("{}", suite.descriptor(index).id);
        return Ok(true);
    }
    auto heading = console::context(
        options.quick, options.diagnose, options.repeats, options.verbose, environment);
    rstd_try(console::write(heading.as_str()));
    ConsoleRenderer renderer({ options.verbose, options.repeats });
    for (auto index : *selected) renderer.include(suite.descriptor(index).id.as_str());
    auto            output = rstd::io::stdout();
    ConsoleReporter reporter { output, rstd::move(renderer) };
    auto            measured =
        options.diagnose
            ? runner.diagnose(
                  suite, options.iterations.is_some() ? *options.iterations : u64(1), reporter)
            : runner.run(suite, reporter);
    if (measured.is_err()) return Err(rstd::move(measured).unwrap_err());
    if (options.json_path.is_some()) {
        auto encoded = report::encode(*measured);
        if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err());
        auto written = rstd::fs::write(options.json_path->as_path(), encoded->as_str().as_bytes());
        if (written.is_err())
            return Err(rstd::format("cannot write report: {}", written.unwrap_err()));
    }
    if (baseline.is_some()) {
        auto comparable = rstd_try(console::write_comparison(
            rstd::bench::compare(*baseline, *measured, comparison_mode), options.verbose));
        return Ok(measured->passed() && comparable);
    }
    return Ok(measured->passed());
}

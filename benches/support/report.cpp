module;
#include <rstd/macro.hpp>

module rstd_benches;
import rstd;
import rstd.bench;
import rstd.json;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
namespace json = rstd::json;

auto number(u64 value) -> json::Value {
    return json::Value::Number(json::Number::from_u64(value));
}
auto real(f64 value) -> json::Value {
    auto result = json::Number::from_f64(value);
    return result.is_some() ? json::Value::Number(*result) : json::Value::Null();
}
auto string(ref<str> value) -> json::Value {
    return json::Value::String(String::make(value));
}
auto nanos(rstd::time::Duration value) -> u64 {
    auto wide = value.as_nanos();
    return wide > u128(u64::MAX.to_primitive()) ? u64::MAX : u64(wide.to_primitive());
}
auto optional_number(Option<u64> value) -> json::Value {
    return value.is_some() ? number(*value) : json::Value::Null();
}
auto optional_real(Option<f64> value) -> json::Value {
    return value.is_some() ? real(*value) : json::Value::Null();
}
auto scope_name(rstd::bench::MeasurementScope scope) -> ref<str> {
    switch (scope) {
    case rstd::bench::MeasurementScope::Repeated: return "repeated"_str;
    case rstd::bench::MeasurementScope::Batched: return "owned-input"_str;
    case rstd::bench::MeasurementScope::BatchedRef: return "borrowed-input"_str;
    }
    return "unknown"_str;
}
auto counters_json(const rstd::bench::CounterSet& counters) -> json::Value {
    auto result                    = json::Value {};
    result["page_faults"_str]      = optional_number(counters.page_faults);
    result["cycles"_str]           = optional_number(counters.cpu_cycles);
    result["context_switches"_str] = optional_number(counters.context_switches);
    result["instructions"_str]     = optional_number(counters.instructions);
    result["branches"_str]         = optional_number(counters.branch_instructions);
    result["branch_misses"_str]    = optional_number(counters.branch_misses);
    return result;
}
auto case_json(const RunResult& result) -> json::Value {
    auto object                = json::Value {};
    object["suite"_str]        = string(text(result.benchmark.m_suite));
    object["name"_str]         = string(text(result.benchmark.m_name));
    auto        params         = json::Value {};
    const auto& input          = result.benchmark.parameters;
    params["n"_str]            = input.n ? number(u64(input.n)) : json::Value::Null();
    params["keys"_str]         = input.keys ? number(u64(input.keys)) : json::Value::Null();
    params["threads"_str]      = input.threads ? number(u64(input.threads)) : json::Value::Null();
    params["distribution"_str] = string(text(input.distribution));
    object["parameters"_str]   = rstd::move(params);
    if (result.outcome.is_Skipped()) {
        object["status"_str] = string("skipped"_str);
        object["reason"_str] = string(result.outcome.as_Skipped().reason.as_str());
        return object;
    }
    if (result.outcome.is_Failed()) {
        object["status"_str] = string("failed"_str);
        auto errors          = json::Array::make();
        for (const auto& error : result.outcome.as_Failed().errors)
            errors.push(string(error.as_str()));
        object["errors"_str] = json::Value::Array(rstd::move(errors));
        return object;
    }
    object["status"_str]             = string("ok"_str);
    const auto& measured             = result.outcome.as_Passed().measurement;
    const auto& config               = measured.config();
    const auto& work                 = measured.run_config();
    auto        summary              = measured.summary();
    object["timing"_str]             = string("wall"_str);
    object["scope"_str]              = string(scope_name(measured.scope()));
    object["output_destruction"_str] = string(
        measured.scope() == rstd::bench::MeasurementScope::Repeated ? "timed"_str : "deferred"_str);
    object["input_destruction"_str] = string(
        measured.scope() == rstd::bench::MeasurementScope::BatchedRef ? "deferred"_str
                                                                      : "operation-owned"_str);
    object["batch_max_items"_str]     = measured.batch_size().is_some()
                                            ? number(u64(measured.batch_size()->to_primitive()))
                                            : json::Value::Null();
    object["units_per_iteration"_str] = real(work.batch);
    object["unit"_str]                = string(work.unit.as_str());
    object["items_per_iteration"_str] = number(work.items_per_iteration);
    object["bytes_per_iteration"_str] = number(work.bytes_per_iteration);
    object["iterations"_str]          = number(summary.total_iterations);
    object["elapsed_ns"_str]          = number(nanos(summary.total_elapsed));
    object["median_ns_per_unit"_str]  = real(summary.median_ns_per_unit);
    object["median_ns_per_op"_str]    = real(summary.median_ns_per_unit * work.batch);
    object["median_ns_per_item"_str]  = work.items_per_iteration != u64()
                                            ? real(summary.median_ns_per_unit * work.batch /
                                                   f64(work.items_per_iteration.to_primitive()))
                                            : json::Value::Null();
    object["mdape"_str]               = real(summary.median_absolute_percentage_error);
    object["clock_resolution_ns"_str] = number(nanos(measured.clock_resolution()));
    auto settings                     = json::Value {};
    settings["epochs"_str]            = number(u64(config.epochs.to_primitive()));
    settings["warmup_iterations"_str] = number(config.warmup_iterations);
    settings["exact_epoch_iterations"_str] = optional_number(config.exact_epoch_iterations);
    settings["min_epoch_ns"_str]           = number(nanos(config.min_epoch_time));
    settings["max_epoch_ns"_str]           = number(nanos(config.max_epoch_time));
    settings["min_epoch_iterations"_str]   = number(config.min_epoch_iterations);
    settings["clock_resolution_multiple"_str] =
        number(u64(config.clock_resolution_multiple.to_primitive()));
    settings["jitter_seed"_str]  = number(config.jitter_seed);
    settings["counter_mode"_str] = string(config.counter_mode.is_Disabled()   ? "disabled"_str
                                          : config.counter_mode.is_Required() ? "required"_str
                                                                              : "auto"_str);
    object["config"_str]         = rstd::move(settings);
    auto epochs                  = json::Array::make();
    for (const auto& epoch : measured.measurements()) {
        auto value              = json::Value {};
        value["iterations"_str] = number(epoch.iterations);
        value["elapsed_ns"_str] = number(nanos(epoch.elapsed));
        value["counters"_str]   = counters_json(epoch.counters);
        epochs.push(rstd::move(value));
    }
    object["epochs"_str]               = json::Value::Array(rstd::move(epochs));
    const auto& availability           = measured.counter_availability();
    object["counter_availability"_str] = string(availability.is_Disabled()    ? "disabled"_str
                                                : availability.is_Available() ? "available"_str
                                                                              : "unavailable"_str);
    if (availability.is_Available())
        object["counter_mask"_str] = number(u64(availability.as_Available().mask.to_primitive()));
    if (availability.is_Unavailable())
        object["counter_error"_str] = json::Value::Number(
            json::Number::from_i64(i64(availability.as_Unavailable().code.to_primitive())));
    object["instructions_per_unit"_str]  = optional_real(summary.instructions_per_unit);
    object["cycles_per_unit"_str]        = optional_real(summary.cycles_per_unit);
    object["instructions_per_cycle"_str] = optional_real(summary.instructions_per_cycle);
    object["branches_per_unit"_str]      = optional_real(summary.branches_per_unit);
    object["branch_miss_ratio"_str]      = optional_real(summary.branch_miss_ratio);
    return object;
}
auto rstd_bench::print_result(const RunResult& result) -> void {
    auto suite = text(result.benchmark.m_suite);
    auto name  = text(result.benchmark.m_name);
    if (result.outcome.is_Passed()) {
        const auto& measured = result.outcome.as_Passed().measurement;
        auto        summary  = measured.summary();
        rstd::io::println("{}.{}: {} ns/op; {} iterations; {}",
                          suite,
                          name,
                          summary.median_ns_per_unit * measured.run_config().batch,
                          summary.total_iterations,
                          scope_name(measured.scope()));
    } else if (result.outcome.is_Skipped()) {
        rstd::io::println("{}.{}: skipped: {}", suite, name, result.outcome.as_Skipped().reason);
    } else {
        for (const auto& error : result.outcome.as_Failed().errors)
            rstd::io::eprintln("{}.{}: failed: {}", suite, name, error);
    }
}
auto rstd_bench::report(const Options& options, const Vec<RunResult>& results) -> json::Value {
    auto document                  = json::Value {};
    document["format_version"_str] = number(u64(1));
    document["mode"_str]           = string(options.quick ? "quick"_str : "normal"_str);
    document["quick_is_performance_evidence"_str] = json::Value::Bool(false);
    auto build                                    = json::Value {};
    build["source"_str]      = string("compiler macros; unavailable fields are unknown"_str);
    build["compiler"_str]    = string(text(__clang_version__));
    const ref<str> unknown[] = { "compiler_path"_str,      "target"_str, "profile"_str,
                                 "optimization_level"_str, "lto"_str,    "standard_library"_str,
                                 "revision"_str,           "dirty"_str };
    for (auto field : unknown) build[field] = string("unknown"_str);
#if __has_feature(cxx_exceptions)
    build["exceptions"_str] = json::Value::Bool(true);
#else
    build["exceptions"_str] = json::Value::Bool(false);
#endif
#if __has_feature(cxx_rtti)
    build["rtti"_str] = json::Value::Bool(true);
#else
    build["rtti"_str] = json::Value::Bool(false);
#endif
    build["address_sanitizer"_str] = json::Value::Bool(__has_feature(address_sanitizer));
    build["thread_sanitizer"_str]  = json::Value::Bool(__has_feature(thread_sanitizer));
    build["memory_sanitizer"_str]  = json::Value::Bool(__has_feature(memory_sanitizer));
    document["build"_str]          = rstd::move(build);
    auto runtime                   = json::Value {};
    runtime["cpu"_str]             = string("unknown"_str);
    runtime["os"_str]              = string("unknown"_str);
    document["runtime"_str]        = rstd::move(runtime);
    auto values                    = json::Array::make();
    for (const auto& result : results) values.push(case_json(result));
    document["results"_str] = json::Value::Array(rstd::move(values));
    return document;
}

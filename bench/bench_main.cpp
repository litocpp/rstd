#include "benchmark.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

import rstd.json;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

#ifndef RSTD_BENCH_BUILD_TYPE
#define RSTD_BENCH_BUILD_TYPE "unknown"
#endif

#ifndef RSTD_BENCH_ASAN
#define RSTD_BENCH_ASAN 0
#endif

namespace
{

struct Options {
    const char*   m_suite { "all" };
    const char*   m_json_path { nullptr };
    std::uint64_t m_iterations { 0 };
    bool          m_quick { false };
    bool          m_list { false };
};

struct RunResult {
    const rstd_bench::BenchCase*   m_case;
    Option<bench::BenchmarkResult> m_measurement;
    Option<bench::BenchError>      m_error;
    bool                           m_ok;
};

auto duration_ns(time::Duration value) -> u64 {
    auto const nanos = value.as_nanos();
    return nanos > u128(u64::MAX.to_primitive()) ? u64::MAX : u64(nanos.to_primitive());
}

auto parse_u64(const char* value) -> std::uint64_t {
    return static_cast<std::uint64_t>(std::strtoull(value, nullptr, 10));
}

auto parse_options(int argc, char** argv) -> Options {
    auto options = Options {};

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--suite") == 0 && i + 1 < argc) {
            options.m_suite = argv[++i];
        } else if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            options.m_iterations = parse_u64(argv[++i]);
        } else if (std::strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            options.m_json_path = argv[++i];
        } else if (std::strcmp(argv[i], "--quick") == 0) {
            options.m_quick = true;
        } else if (std::strcmp(argv[i], "--list") == 0) {
            options.m_list = true;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: rstd_bench [--suite all|alloc|sync|async|net] [--quick] "
                        "[--iterations N] [--json PATH] [--list]\n");
            std::exit(0);
        }
    }

    return options;
}

auto suite_matches(const Options& options, const rstd_bench::BenchCase& benchmark) -> bool {
    return std::strcmp(options.m_suite, "all") == 0 ||
           std::strcmp(options.m_suite, benchmark.m_suite) == 0;
}

auto make_config(const Options& options, const rstd_bench::BenchCase& benchmark)
    -> bench::BenchConfig {
    auto config = bench::BenchConfig {};
    if (options.m_quick) {
        config.epochs                 = usize(3);
        config.exact_epoch_iterations = Some(u64(benchmark.m_quick_iterations));
        config.warmup_iterations      = u64(1);
        config.counter_mode           = bench::CounterMode::Disabled();
    }
    if (options.m_iterations != 0) {
        config.exact_epoch_iterations = Some(u64(options.m_iterations));
    }
    return config;
}

auto run_case(const Options& options, const rstd_bench::BenchCase& benchmark) -> RunResult {
    auto result = benchmark.m_run(make_config(options, benchmark));
    if (result.measurement.is_err()) {
        return RunResult {
            .m_case        = &benchmark,
            .m_measurement = None(),
            .m_error       = Some(rstd::move(result.measurement).unwrap_err_unchecked()),
            .m_ok          = false,
        };
    }
    return RunResult {
        .m_case        = &benchmark,
        .m_measurement = Some(rstd::move(result.measurement).unwrap_unchecked()),
        .m_error       = None(),
        .m_ok          = result.ok,
    };
}

void print_result(const RunResult& result) {
    if (result.m_measurement.is_none()) {
        std::printf("%-8s %-32s %10s %20s %13s failed\n",
                    result.m_case->m_suite,
                    result.m_case->m_name,
                    "-",
                    "-",
                    "-");
        return;
    }

    auto summary = result.m_measurement->summary();
    auto total_ms =
        static_cast<double>(duration_ns(summary.total_elapsed).to_primitive()) / 1'000'000.0;
    std::printf("%-8s %-32s %10llu %12.2f ns/op %10.3f ms %s\n",
                result.m_case->m_suite,
                result.m_case->m_name,
                static_cast<unsigned long long>(summary.total_iterations.to_primitive()),
                summary.median_ns_per_unit.to_primitive(),
                total_ms,
                result.m_ok ? "ok" : "failed");
}

auto json_u64(u64 value) -> json::Value {
    return json::Value::Number(json::Number::from_u64(value));
}

auto json_i64(i64 value) -> json::Value {
    return json::Value::Number(json::Number::from_i64(value));
}

auto json_f64(f64 value) -> json::Value {
    auto number = json::Number::from_f64(value);
    return number.is_some() ? json::Value::Number(*number) : json::Value::Null();
}

void add_optional_f64(json::Value& object, ref<str> key, Option<f64> value) {
    object[key] = value.is_some() ? json_f64(*value) : json::Value::Null();
}

auto to_json(const RunResult& result) -> json::Value {
    auto object              = json::Value {};
    object["suite"_str]      = json::Value::String(String::make(
        rstd::cppstd::as_str(std::string_view(result.m_case->m_suite)).unwrap_unchecked()));
    object["name"_str]       = json::Value::String(String::make(
        rstd::cppstd::as_str(std::string_view(result.m_case->m_name)).unwrap_unchecked()));
    object["build_type"_str] = json::Value::String(String::make(
        rstd::cppstd::as_str(std::string_view(RSTD_BENCH_BUILD_TYPE)).unwrap_unchecked()));
    object["asan"_str]       = json::Value::Bool(RSTD_BENCH_ASAN != 0);
    object["ok"_str]         = json::Value::Bool(result.m_ok);

    if (result.m_measurement.is_none()) {
        object["iterations"_str]   = json_u64(u64());
        object["elapsed_ns"_str]   = json_u64(u64());
        object["ns_per_iter"_str]  = json_f64(f64());
        object["items"_str]        = json_u64(u64());
        object["bytes"_str]        = json_u64(u64());
        object["epochs"_str]       = json_u64(u64());
        object["measurements"_str] = json::Value::Array(json::Array::make());
        return object;
    }

    const auto& measurement = *result.m_measurement;
    auto        summary     = measurement.summary();
    auto        elapsed_ns  = duration_ns(summary.total_elapsed);
    auto ns_per_iter = summary.total_iterations == u64()
                           ? f64()
                           : f64(static_cast<double>(elapsed_ns.to_primitive()) /
                                 static_cast<double>(summary.total_iterations.to_primitive()));
    object["iterations"_str]  = json_u64(summary.total_iterations);
    object["elapsed_ns"_str]  = json_u64(elapsed_ns);
    object["ns_per_iter"_str] = json_f64(ns_per_iter);
    object["items"_str]       = json_u64(summary.total_items);
    object["bytes"_str]       = json_u64(summary.total_bytes);
    object["epochs"_str]      = json_u64(u64(measurement.measurements().len().to_primitive()));
    object["median_ns_per_unit"_str]  = json_f64(summary.median_ns_per_unit);
    object["mdape"_str]               = json_f64(summary.median_absolute_percentage_error);
    object["clock_resolution_ns"_str] = json_u64(duration_ns(measurement.clock_resolution()));
    object["jitter_seed"_str]         = json_u64(measurement.config().jitter_seed);

    auto epochs = json::Array::with_capacity(measurement.measurements().len());
    for (usize index; index < measurement.measurements().len(); ++index) {
        const auto& epoch             = measurement.measurements()[index];
        auto        epoch_value       = json::Value {};
        epoch_value["iterations"_str] = json_u64(epoch.iterations);
        epoch_value["elapsed_ns"_str] = json_u64(duration_ns(epoch.elapsed));
        epochs.push(rstd::move(epoch_value));
    }
    object["measurements"_str] = json::Value::Array(rstd::move(epochs));

    const auto& availability = measurement.counter_availability();
    if (availability.is_Disabled()) {
        object["counter_availability"_str] = json::Value::String(String::make("disabled"_str));
    } else if (availability.is_Available()) {
        object["counter_availability"_str] = json::Value::String(String::make("available"_str));
        object["counter_mask"_str] = json_u64(u64(availability.as_Available().mask.to_primitive()));
    } else {
        object["counter_availability"_str] = json::Value::String(String::make("unavailable"_str));
        object["counter_error"_str] =
            json_i64(i64(availability.as_Unavailable().code.to_primitive()));
    }
    add_optional_f64(object, "instructions_per_unit"_str, summary.instructions_per_unit);
    add_optional_f64(object, "cycles_per_unit"_str, summary.cycles_per_unit);
    add_optional_f64(object, "instructions_per_cycle"_str, summary.instructions_per_cycle);
    add_optional_f64(object, "branches_per_unit"_str, summary.branches_per_unit);
    add_optional_f64(object, "branch_miss_ratio"_str, summary.branch_miss_ratio);
    return object;
}

auto write_json(const char* path, const Vec<RunResult>& results) -> bool {
    auto values = json::Array::with_capacity(results.len());
    for (const auto& result : results) values.push(to_json(result));
    auto output = json::to_string(json::Value::Array(rstd::move(values)),
                                  json::FormatOptions { .pretty = true });

    auto* file = std::fopen(path, "w");
    if (file == nullptr) return false;
    auto const written = std::fwrite(output.data(), 1, output.size().to_primitive(), file);
    auto const closed  = std::fclose(file);
    return written == output.size().to_primitive() && closed == 0;
}

template<std::size_t N>
void append_list(rstd_bench::BenchCase const* (&cases)[N],
                 std::size_t (&lens)[N],
                 std::size_t           index,
                 rstd_bench::BenchList list) {
    cases[index] = list.m_cases;
    lens[index]  = list.m_len;
}

} // namespace

auto main(int argc, char** argv) -> int {
    auto options = parse_options(argc, argv);

    rstd_bench::BenchCase const* suites[4] {};
    std::size_t                  lens[4] {};
    append_list(suites, lens, 0, rstd_bench::alloc_benchmarks());
    append_list(suites, lens, 1, rstd_bench::sync_benchmarks());
    append_list(suites, lens, 2, rstd_bench::async_benchmarks());
    append_list(suites, lens, 3, rstd_bench::net_benchmarks());

    if (options.m_list) {
        for (std::size_t i = 0; i < 4; ++i) {
            for (std::size_t j = 0; j < lens[i]; ++j) {
                if (suite_matches(options, suites[i][j])) {
                    std::printf("%s.%s\n", suites[i][j].m_suite, suites[i][j].m_name);
                }
            }
        }
        return 0;
    }

    auto results = Vec<RunResult>::make();
    bool all_ok  = true;

    std::printf(
        "%-8s %-32s %10s %20s %13s %s\n", "suite", "name", "iters", "time", "total", "status");
    std::printf("build=%s asan=%s\n", RSTD_BENCH_BUILD_TYPE, RSTD_BENCH_ASAN ? "true" : "false");

    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = 0; j < lens[i]; ++j) {
            const auto& benchmark = suites[i][j];
            if (! suite_matches(options, benchmark)) continue;
            auto result = run_case(options, benchmark);
            all_ok      = all_ok && result.m_ok;
            print_result(result);
            results.push(rstd::move(result));
        }
    }

    if (options.m_json_path != nullptr && ! write_json(options.m_json_path, results)) {
        std::fprintf(stderr, "failed to write json output: %s\n", options.m_json_path);
        return 1;
    }
    if (results.is_empty()) {
        std::fprintf(stderr, "no benchmark cases selected\n");
        return 1;
    }
    return all_ok ? 0 : 1;
}

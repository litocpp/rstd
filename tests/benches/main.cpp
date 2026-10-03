#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import rstd.bench;
import rstd.json;
import rstd_benches;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
namespace bench = rstd::bench;

auto main() -> int {
    return rstd::test::run_registered().to_primitive();
}

template<typename... Args>
auto arguments(Args... args) -> Vec<rstd::ffi::OsString> {
    auto result = Vec<rstd::ffi::OsString>::make();
    result.push(rstd::ffi::OsString::from("bench"_str));
    (result.push(rstd::ffi::OsString::from(args)), ...);
    return result;
}

TEST(BenchRunner, RejectsInvalidArgumentsAndEmptySelection) {
    EXPECT_TRUE(parse_options(arguments("--unknown"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--suite"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--iterations"_str, "0"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--iterations"_str, "-1"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--iterations"_str, "18446744073709551616"_str)).is_err());
    auto parsed = parse_options(arguments("--quick"_str, "--iterations=7"_str, "--list"_str));
    ASSERT_TRUE(parsed.is_ok());
    EXPECT_TRUE(parsed->quick);
    EXPECT_TRUE(parsed->list);
    EXPECT_EQ(*parsed->iterations, u64(7));
    EXPECT_TRUE(parse_options(arguments("--help"_str))->display.is_some());
    EXPECT_TRUE(parse_options(arguments("--diagnose"_str))->diagnose);
    bench::Suite suite;
    EXPECT_TRUE(bench::Runner().select(suite).is_err());
}

TEST(BenchRunner, MeasurementAndCleanupFailuresBothSurvive) {
    auto config    = bench::BenchConfig {};
    config.epochs  = usize();
    auto calls     = usize();
    auto cleaned   = false;
    auto validated = false;
    auto runner    = bench::Bench::new_(rstd::move(config));
    auto outcome   = measure_case(
        "failed"_str,
        runner,
        {},
        [&] {
            ++calls;
        },
        [&] {
            validated = cleaned;
            return true;
        },
        [&]() -> Result<empty, String> {
            cleaned = true;
            return Err("cleanup failed"_Str);
        });
    ASSERT_TRUE(outcome.is_Failed());
    EXPECT_EQ(calls, usize());
    EXPECT_TRUE(cleaned);
    EXPECT_TRUE(validated);
    ASSERT_EQ(outcome.as_Failed().errors.len(), usize(2));
    EXPECT_EQ(outcome.as_Failed().errors[usize(1)].as_str(), "cleanup failed"_str);
}

TEST(BenchRunner, ParsesSamplingAndComparisonOptions) {
    auto parsed = parse_options(arguments("--epochs=4"_str,
                                          "--warmup=2"_str,
                                          "--iterations=8"_str,
                                          "--repeats=3"_str,
                                          "--counters=auto"_str,
                                          "--stop-on-failure"_str));
    ASSERT_TRUE(parsed.is_ok());
    EXPECT_EQ(*parsed->epochs, usize(4));
    EXPECT_EQ(*parsed->warmup, u64(2));
    EXPECT_EQ(*parsed->iterations, u64(8));
    EXPECT_EQ(parsed->repeats, u64(3));
    EXPECT_TRUE(parsed->counters.is_Auto());
    EXPECT_TRUE(parsed->stop_on_failure);
    auto compare = parse_options(arguments(
        "--baseline=before.json"_str, "--compare=after.json"_str, "--cross-environment"_str));
    ASSERT_TRUE(compare.is_ok());
    EXPECT_TRUE(compare->baseline.is_some());
    EXPECT_TRUE(compare->candidate.is_some());
    EXPECT_TRUE(compare->cross_environment);
    EXPECT_TRUE(parse_options(arguments("--epochs=0"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--repeats=0"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--counters=invalid"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--compare=after.json"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--cross-environment"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--baseline=a"_str, "--quick"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--quick"_str, "--epochs=2"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--diagnose"_str, "--counters=required"_str)).is_err());
    EXPECT_TRUE(parse_options(arguments("--baseline=a"_str, "--compare=b"_str, "--repeats=1"_str))
                    .is_err());
}

TEST(BenchRunner, RealCasesPreserveScopesAndProduceThroughput) {
    auto created = make_suite();
    ASSERT_TRUE(created.is_ok());
    auto suite = rstd::move(created).unwrap();
    EXPECT_EQ(suite.len(), usize(81));
    const ref<str> groups[] { "alloc"_str, "slice"_str, "map"_str, "set"_str };
    usize          cases;
    for (auto group : groups) {
        bench::RunnerOptions options;
        options.suite                           = Some(String::make(group));
        options.quick                           = true;
        options.repeats                         = u64(2);
        options.sampling.exact_epoch_iterations = Some(u64(1));
        bench::NullReporter sink;
        auto                report = bench::Runner(rstd::move(options)).run(suite, sink);
        ASSERT_TRUE(report.is_ok());
        EXPECT_TRUE(report->passed());
        EXPECT_TRUE(report->quick);
        for (const auto& result : report->results) {
            ++cases;
            ASSERT_TRUE(result.outcome.is_Passed());
            const auto& measured = result.outcome.as_Passed().measurement;
            auto        batched =
                group == "slice"_str || result.descriptor.name == "vec_push_reserved"_str;
            EXPECT_EQ(measured.scope(),
                      batched ? bench::MeasurementScope::BatchedRef
                              : bench::MeasurementScope::Repeated);
            EXPECT_EQ(measured.measurements().len(), usize(3));
            for (const auto& epoch : measured.measurements()) EXPECT_EQ(epoch.iterations, u64(1));
            auto rendered = ConsoleRenderer({ false, u64(2) }).render(result);
            EXPECT_TRUE(rendered.as_str().contains("items/s"_str));
            EXPECT_TRUE(
                rendered.as_str().contains(result.repetition == u64() ? "1/2"_str : "2/2"_str));
            if (group == "alloc"_str) EXPECT_TRUE(rendered.as_str().contains("bytes/s"_str));
        }
    }
    EXPECT_EQ(cases, usize(102));
}

TEST(BenchRunner, RealMapDiagnosticsSeparateSetupFromOperations) {
    auto created = make_suite();
    ASSERT_TRUE(created.is_ok());
    auto                 suite = rstd::move(created).unwrap();
    bench::RunnerOptions options;
    options.suite = Some("map"_Str);
    bench::NullReporter sink;
    auto                report = bench::Runner(rstd::move(options)).diagnose(suite, u64(2), sink);
    ASSERT_TRUE(report.is_ok());
    EXPECT_TRUE(report->passed());
    EXPECT_EQ(report->results.len(), usize(16));
    usize lookups, zero_hits, skipped;
    for (const auto& result : report->results) {
        if (result.descriptor.implementation == "cppstd"_str) {
            EXPECT_TRUE(result.outcome.is_Skipped());
            ++skipped;
            continue;
        }
        ASSERT_TRUE(result.outcome.is_Diagnosed());
        const auto& measured = result.outcome.as_Diagnosed().measurement;
        ASSERT_TRUE(measured.allocations.is_some());
        const auto& allocation = *measured.allocations;
        EXPECT_TRUE(allocation.operation.before.live_bytes > u64());
        EXPECT_EQ(allocation.operation.after.live_bytes, allocation.operation.before.live_bytes);
        EXPECT_EQ(allocation.lifetime.live_bytes, u64());
        EXPECT_EQ(allocation.lifetime.allocations, allocation.lifetime.deallocations);
        EXPECT_EQ(measured.iterations, u64(2));
        if (result.descriptor.name.as_str().starts_with("find/"_str)) {
            ++lookups;
            EXPECT_EQ(allocation.operation.after.allocations,
                      allocation.operation.before.allocations);
            EXPECT_EQ(allocation.operation.after.requested_bytes,
                      allocation.operation.before.requested_bytes);
            for (const auto& parameter : result.descriptor.parameters)
                if (parameter.name == "hit_percent"_str &&
                    parameter.value.as_Unsigned().value == u64())
                    ++zero_hits;
        } else {
            EXPECT_TRUE(allocation.operation.after.allocations >
                        allocation.operation.before.allocations);
        }
    }
    EXPECT_EQ(lookups, usize(6));
    EXPECT_EQ(zero_hits, usize(2));
    EXPECT_EQ(skipped, usize(8));
}

TEST(BenchRunner, CollectionPairsShareInputsAndHashPolicy) {
    bench::Suite suite;
    ASSERT_TRUE(add_collection_cases(suite).is_ok());
    ASSERT_EQ(suite.len(), usize(32));
    for (usize i; i < suite.len(); i += usize(2)) {
        const auto& local    = suite.descriptor(i);
        const auto& standard = suite.descriptor(i + usize(1));
        EXPECT_EQ(local.implementation, "rstd"_str);
        EXPECT_EQ(standard.implementation, "cppstd"_str);
        EXPECT_EQ(local.suite, standard.suite);
        EXPECT_EQ(standard.name, rstd::format("{}/cppstd", local.name));
        ASSERT_EQ(local.parameters.len(), standard.parameters.len());
        for (usize j; j < local.parameters.len(); ++j) {
            const auto& a = local.parameters[j];
            const auto& b = standard.parameters[j];
            EXPECT_EQ(a.name, b.name);
            if (a.name == "allocator"_str) {
                EXPECT_EQ(a.value.as_Text().value, "alloc::Global"_str);
                EXPECT_EQ(b.value.as_Text().value, "std::allocator"_str);
            } else if (a.value.is_Unsigned()) {
                ASSERT_TRUE(b.value.is_Unsigned());
                EXPECT_EQ(a.value.as_Unsigned().value, b.value.as_Unsigned().value);
            } else {
                ASSERT_TRUE(a.value.is_Text());
                ASSERT_TRUE(b.value.is_Text());
                EXPECT_EQ(a.value.as_Text().value, b.value.as_Text().value);
            }
        }
    }
    bench::RunnerOptions options;
    options.suite = Some("set"_Str);
    bench::NullReporter sink;
    auto                report = bench::Runner(rstd::move(options)).diagnose(suite, u64(1), sink);
    ASSERT_TRUE(report.is_ok());
    ASSERT_EQ(report->results.len(), usize(16));
    for (const auto& result : report->results) EXPECT_TRUE(result.outcome.is_Skipped());
}

TEST(BenchRunner, RealAllocDiagnosticsDoNotPretendToMeasureUnsupportedWindows) {
    auto created = make_suite();
    ASSERT_TRUE(created.is_ok());
    auto                 suite = rstd::move(created).unwrap();
    bench::RunnerOptions options;
    options.suite = Some("alloc"_Str);
    bench::NullReporter sink;
    auto                report = bench::Runner(rstd::move(options)).diagnose(suite, u64(2), sink);
    ASSERT_TRUE(report.is_ok());
    EXPECT_EQ(report->results.len(), usize(4));
    usize skipped;
    for (const auto& result : report->results) {
        if (result.descriptor.name != "vec_push_end_to_end"_str) {
            EXPECT_TRUE(result.outcome.is_Skipped());
            ++skipped;
            continue;
        }
        ASSERT_TRUE(result.outcome.is_Diagnosed());
        const auto& measured = result.outcome.as_Diagnosed().measurement;
        ASSERT_TRUE(measured.allocations.is_some());
        const auto& allocation = *measured.allocations;
        EXPECT_EQ(allocation.operation.before.live_bytes, u64(64 * sizeof(i32)));
        EXPECT_EQ(allocation.operation.after.allocations - allocation.operation.before.allocations,
                  u64(2));
        EXPECT_EQ(allocation.operation.after.requested_bytes -
                      allocation.operation.before.requested_bytes,
                  u64(128 * sizeof(i32)));
        EXPECT_EQ(allocation.lifetime.live_bytes, u64());
    }
    EXPECT_EQ(skipped, usize(3));
}

TEST(BenchRunner, FailedAndSkippedReportsContainNoPerformanceSamples) {
    bench::RunReport report;
    report.quick = true;
    report.results.push(bench::RunResult {
        { "test.failed"_Str, "test"_Str, "1"_Str, "rstd"_Str },
        complete_measurement(
            Err(bench::BenchError::CounterUnavailable(i32(42))), false, Err("join failed"_Str)) });
    report.results.push(bench::RunResult { { "test.skipped"_Str, "test"_Str, "1"_Str, "rstd"_Str },
                                           CaseRunResult::Skipped("unsupported backend"_Str) });
    auto encoded = rstd_bench::report::encode(report);
    ASSERT_TRUE(encoded.is_ok());
    auto rendered = rstd::move(encoded).unwrap();
    EXPECT_TRUE(rendered.as_str().contains("\"format_version\":3"_str));
    EXPECT_TRUE(rendered.as_str().contains("\"status\":\"failed\""_str));
    EXPECT_TRUE(rendered.as_str().contains("\"status\":\"skipped\""_str));
    EXPECT_TRUE(rendered.as_str().contains("join failed"_str));
    EXPECT_TRUE(rendered.as_str().contains("42"_str));
    EXPECT_FALSE(rendered.as_str().contains("elapsed_ns"_str));
    EXPECT_FALSE(rendered.as_str().contains("median_ns_per_op"_str));
}

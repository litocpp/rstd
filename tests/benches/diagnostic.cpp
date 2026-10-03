#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import rstd.bench;
import rstd_benches;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace bench = rstd::bench;

using TrackedVec = Vec<u64, rstd::alloc::TrackingAllocator<>>;

struct DiagnosticTestState {
    usize prepared;
    usize finished;
    usize calls;
    bool  fail_prepare {};
    bool  fail_check {};
    bool  fail_run {};
    bool  fail_finish {};
};

struct AllocationSession {
    DiagnosticTestState*             state;
    rstd::alloc::TrackingAllocator<> allocator;
    TrackedVec                       input;
    Option<TrackedVec>               output;

    auto check() -> Result<empty, String> {
        if (state->fail_check) return Err("check failed"_Str);
        return Ok(empty {});
    }
    auto run(u64 iterations) -> Result<bench::DiagnosticMeasurement, String> {
        state->calls += usize(iterations.to_primitive());
        output = Some(TrackedVec::with_capacity_in(usize(8), allocator));
        if (state->fail_run) return Err("operation failed"_Str);
        bench::DiagnosticMeasurement result;
        result.metrics.push({ "items"_Str,
                              bench::DiagnosticUnit::Count,
                              bench::MetricDirection::Neutral,
                              "operation"_Str,
                              "test"_Str,
                              bench::DiagnosticValue::Count(u64()) });
        result.metrics.push({ "ratio"_Str,
                              bench::DiagnosticUnit::Ratio,
                              bench::MetricDirection::Neutral,
                              "operation"_Str,
                              "test"_Str,
                              bench::DiagnosticValue::Ratio(u64::MAX, u64(2)) });
        result.metrics.push({ "rss"_Str,
                              bench::DiagnosticUnit::Bytes,
                              bench::MetricDirection::Lower,
                              "process"_Str,
                              "test"_Str,
                              bench::DiagnosticValue::Unavailable("not sampled"_Str) });
        result.metrics.push({ "overflow"_Str,
                              bench::DiagnosticUnit::Count,
                              bench::MetricDirection::Neutral,
                              "operation"_Str,
                              "test"_Str,
                              bench::DiagnosticValue::Overflow() });
        return Ok(rstd::move(result));
    }
    auto finish() -> Result<empty, String> {
        ++state->finished;
        if (state->fail_finish) return Err("finish failed"_Str);
        return Ok(empty {});
    }
};

auto diagnostic_test_suite(DiagnosticTestState& state) -> bench::Suite {
    bench::Suite suite;
    auto         added = suite.add_function({ "allocation"_Str, "test"_Str, "1"_Str, "rstd"_Str },
                                            [](bench::Bench&, ref<str>) -> bench::CaseOutcome {
                                        return bench::failed("timing must not run"_Str);
                                            });
    added.unwrap();
    suite
        .add_diagnostic(
            "allocation"_str,
            bench::diagnostic_factory(
                [&state](bench::DiagnosticContext& context) -> Result<AllocationSession, String> {
                    ++state.prepared;
                    auto allocator = context.allocator();
                    auto input     = TrackedVec::with_capacity_in(usize(4), allocator);
                    if (state.fail_prepare) return Err("prepare failed"_Str);
                    return Ok(AllocationSession { &state, allocator, rstd::move(input), None() });
                }))
        .unwrap();
    return suite;
}

struct DiagnosticReporter {
    DiagnosticTestState* state;
    usize                reports;
    bool                 fail {};
    auto                 report(const bench::RunResult& result) -> Result<empty, String> {
        ++reports;
        if (state->finished != reports) return Err("report before finish"_Str);
        if (result.outcome.is_Diagnosed() &&
            result.outcome.as_Diagnosed().measurement.allocations->lifetime.live_bytes != u64())
            return Err("report before destruction"_Str);
        if (fail) return Err("report failed"_Str);
        return Ok(empty {});
    }
};

TEST(BenchDiagnostic, FreshSessionsPreserveWindowAndLifetimeBoundaries) {
    DiagnosticTestState  state;
    auto                 suite = diagnostic_test_suite(state);
    bench::RunnerOptions options;
    options.repeats = u64(2);
    bench::Runner runner(rstd::move(options));
    ASSERT_TRUE(runner.select(suite).is_ok());
    EXPECT_EQ(state.prepared, usize());
    DiagnosticReporter sink { &state };
    auto               report = runner.diagnose(suite, u64(3), sink);
    ASSERT_TRUE(report.is_ok());
    ASSERT_TRUE(report->passed());
    ASSERT_TRUE(report->validate().is_ok());
    EXPECT_EQ(report->mode, bench::RunMode::Diagnostic);
    EXPECT_EQ(state.prepared, usize(2));
    EXPECT_EQ(state.finished, usize(2));
    EXPECT_EQ(state.calls, usize(6));
    for (const auto& result : report->results) {
        ASSERT_TRUE(result.outcome.is_Diagnosed());
        const auto& measured = result.outcome.as_Diagnosed().measurement;
        EXPECT_EQ(measured.iterations, u64(3));
        ASSERT_TRUE(measured.allocations.is_some());
        const auto& a = *measured.allocations;
        EXPECT_EQ(a.operation.before.live_bytes, u64(4 * sizeof(u64)));
        EXPECT_EQ(a.operation.after.live_bytes, u64(12 * sizeof(u64)));
        EXPECT_EQ(a.operation.peak_live_bytes, u64(12 * sizeof(u64)));
        EXPECT_EQ(a.operation.after.allocations - a.operation.before.allocations, u64(1));
        EXPECT_EQ(a.lifetime.allocations, u64(2));
        EXPECT_EQ(a.lifetime.deallocations, u64(2));
        EXPECT_EQ(a.lifetime.live_bytes, u64());
    }
    auto encoded = rstd_bench::report::encode(*report);
    ASSERT_TRUE(encoded.is_ok());
    EXPECT_FALSE(encoded->as_str().contains("elapsed_ns"_str));
    auto decoded = rstd_bench::report::decode(encoded->as_str());
    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded->mode, bench::RunMode::Diagnostic);
    const auto& metrics = decoded->results[usize()].outcome.as_Diagnosed().measurement.metrics;
    EXPECT_EQ(metrics[usize()].value.as_Count().value, u64());
    EXPECT_EQ(metrics[usize(1)].value.as_Ratio().numerator, u64::MAX);
    EXPECT_EQ(metrics[usize(1)].value.as_Ratio().denominator, u64(2));
    EXPECT_EQ(metrics[usize(2)].value.as_Unavailable().reason.as_str(), "not sampled"_str);
    EXPECT_TRUE(metrics[usize(3)].value.is_Overflow());
    auto compared = bench::compare(*report, *decoded, bench::ComparisonMode::CrossEnvironment);
    ASSERT_EQ(compared.cases.len(), usize(1));
    EXPECT_TRUE(compared.cases[usize()].candidate_over_baseline.is_none());
    decoded->mode = bench::RunMode::Timing;
    EXPECT_TRUE(decoded->validate().is_err());
}

TEST(BenchDiagnostic, ErrorsFinishSessionsAndReporterFailureStopsRun) {
    for (int phase = 0; phase < 3; ++phase) {
        DiagnosticTestState state;
        state.fail_prepare        = phase == 0;
        state.fail_check          = phase == 1;
        state.fail_run            = phase == 2;
        state.fail_finish         = true;
        auto                suite = diagnostic_test_suite(state);
        bench::NullReporter sink;
        auto                report = bench::Runner().diagnose(suite, u64(1), sink);
        ASSERT_TRUE(report.is_ok());
        EXPECT_FALSE(report->passed());
        auto& outcome = report->results[usize()].outcome;
        ASSERT_TRUE(outcome.is_Failed());
        EXPECT_EQ(state.finished, phase == 0 ? usize() : usize(1));
        EXPECT_EQ(outcome.as_Failed().errors.len(), phase == 0 ? usize(1) : usize(2));
    }
    DiagnosticTestState  state;
    auto                 suite = diagnostic_test_suite(state);
    bench::RunnerOptions options;
    options.repeats = u64(2);
    DiagnosticReporter sink { &state, {}, true };
    auto               result = bench::Runner(rstd::move(options)).diagnose(suite, u64(1), sink);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().as_str(), "report failed"_str);
    EXPECT_EQ(state.finished, usize(1));
}

TEST(BenchDiagnostic, UnregisteredDiagnosticsAreSkippedAndInvalidMetricsRejected) {
    bench::Suite suite;
    suite
        .add_function({ "unsupported"_Str, "test"_Str, "1"_Str, "rstd"_Str },
                      [](bench::Bench&, ref<str>) {
                          return bench::failed("must not execute timing"_Str);
                      })
        .unwrap();
    bench::NullReporter sink;
    auto                report = bench::Runner().diagnose(suite, u64(1), sink);
    ASSERT_TRUE(report.is_ok());
    EXPECT_TRUE(report->results[usize()].outcome.is_Skipped());
    EXPECT_TRUE(bench::Runner().diagnose(suite, u64(), sink).is_err());
    bench::DiagnosticMeasurement invalid;
    invalid.iterations = u64(1);
    EXPECT_TRUE(invalid.validate().is_ok());
    invalid.metrics.push({ "load"_Str,
                           bench::DiagnosticUnit::Ratio,
                           bench::MetricDirection::Neutral,
                           "map"_Str,
                           "test"_Str,
                           bench::DiagnosticValue::Ratio(u64(1), u64()) });
    EXPECT_TRUE(invalid.validate().is_err());
    invalid.metrics[usize()].value = bench::DiagnosticValue::Unavailable("not supported"_Str);
    EXPECT_TRUE(invalid.validate().is_ok());
    invalid.allocation_unavailable = {};
    EXPECT_TRUE(invalid.validate().is_err());
}

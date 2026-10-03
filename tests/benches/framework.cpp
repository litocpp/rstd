#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import rstd.bench;
import rstd_benches;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace bench = rstd::bench;

struct SuiteClock {
    mutable u64 value;
    auto        now_ns() const noexcept -> u64 {
        value += u64(100);
        return value;
    }
    auto resolution() const noexcept -> Result<rstd::time::Duration, bench::ClockError> {
        return Ok(rstd::time::Duration::from_nanos(u64(1)));
    }
};

struct CaseReporter {
    usize* finished;
    usize  reports;
    bool   fail {};
    auto   report(const bench::RunResult&) -> Result<empty, String> {
        ++reports;
        if (*finished != reports) return Err("reported before finish"_Str);
        if (fail) return Err("report failed"_Str);
        return Ok(empty {});
    }
};

auto case_descriptor(ref<str> id) -> bench::CaseDescriptor {
    return { String::make(id), "test"_Str, "1"_Str, "rstd"_Str };
}

TEST(BenchFramework, OwnsFactoriesAndRecreatesSessionsBeforeReporting) {
    using Engine = bench::BasicBench<SuiteClock>;
    bench::BasicSuite<Engine> suite;
    usize                     prepared, finished, calls;
    auto                      factory = bench::factory([&] {
        ++prepared;
        auto work    = bench::repeated([&] {
            ++calls;
            return usize(calls);
        });
        auto session = bench::session(
            rstd::move(work),
            []() -> Result<empty, String> {
                return Ok(empty {});
            },
            [&]() -> Result<empty, String> {
                ++finished;
                return Ok(empty {});
            });
        return Result<decltype(session), String>(Ok(rstd::move(session)));
    });
    ASSERT_TRUE(suite.add(case_descriptor("repeat"_str), rstd::move(factory)).is_ok());
    bench::RunnerOptions options;
    options.repeats                         = u64(2);
    options.sampling.epochs                 = usize(1);
    options.sampling.exact_epoch_iterations = Some(u64(3));
    options.sampling.counter_mode           = bench::CounterMode::Disabled();
    bench::Runner runner(rstd::move(options));
    auto          selected = runner.select(suite);
    ASSERT_TRUE(selected.is_ok());
    EXPECT_EQ(prepared, usize());
    CaseReporter reporter { &finished };
    auto         result = runner.run_with(
        suite,
        [](bench::BenchConfig config) {
            return Engine(SuiteClock {}, rstd::move(config));
        },
        reporter);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result->passed());
    EXPECT_EQ(prepared, usize(2));
    EXPECT_EQ(finished, usize(2));
    EXPECT_EQ(calls, usize(6));
    EXPECT_EQ(result->results.len(), usize(2));
    EXPECT_EQ(result->results[usize(1)].repetition, u64(1));
}

TEST(BenchFramework, PrecheckFailureStillFinishesAndPreservesBothErrors) {
    bench::Suite suite;
    usize        finished;
    auto         factory = bench::factory([&] {
        auto session = bench::session(
            bench::repeated([] {
            }),
            []() -> Result<empty, String> {
                return Err("precheck failed"_Str);
            },
            [&]() -> Result<empty, String> {
                ++finished;
                return Err("finish failed"_Str);
            });
        return Result<decltype(session), String>(Ok(rstd::move(session)));
    });
    ASSERT_TRUE(suite.add(case_descriptor("failure"_str), rstd::move(factory)).is_ok());
    auto engine  = bench::Bench::new_();
    auto outcome = suite.execute(usize(), engine);
    ASSERT_TRUE(outcome.is_Failed());
    EXPECT_EQ(finished, usize(1));
    ASSERT_EQ(outcome.as_Failed().errors.len(), usize(2));
    EXPECT_EQ(outcome.as_Failed().errors[usize()].as_str(), "precheck failed"_str);
    EXPECT_EQ(outcome.as_Failed().errors[usize(1)].as_str(), "finish failed"_str);
}

TEST(BenchFramework, DescriptorRejectsDuplicateParametersAndPreservesZero) {
    auto descriptor = case_descriptor("parameters"_str);
    descriptor.parameters.push({ "n"_Str, bench::ParameterValue::Unsigned(u64()) });
    ASSERT_TRUE(descriptor.validate().is_ok());
    auto copied = descriptor.clone();
    EXPECT_EQ(copied.parameters[usize()].value.as_Unsigned().value, u64());
    descriptor.parameters.push({ "n"_Str, bench::ParameterValue::Unsigned(u64(1)) });
    EXPECT_TRUE(descriptor.validate().is_err());
}

struct TestCollector {
    Vec<u64>* events;
    u64       id;
    bool      fail_begin {};
    bool      fail_end {};
    bool      fail_abort {};
    auto      identity() const -> String { return rstd::format("test/v1/{}", id); }
    auto      availability() const -> bench::CounterAvailability {
        return bench::CounterAvailability::Disabled();
    }
    auto begin() -> Result<empty, bench::BenchError> {
        events->push(id * u64(10));
        if (fail_begin) return Err(bench::BenchError::Collector("begin failed"_Str));
        return Ok(empty {});
    }
    auto end(u64 iterations) -> Result<bench::WindowMeasurement, bench::BenchError> {
        events->push(id * u64(10) + u64(1));
        if (fail_end) return Err(bench::BenchError::Collector("end failed"_Str));
        bench::WindowMeasurement result;
        result.metrics.push({ rstd::format("collector-{}", id),
                              bench::MetricUnit::Count,
                              bench::MetricAggregation::Sum,
                              bench::MetricDirection::Neutral,
                              iterations });
        return Ok(rstd::move(result));
    }
    auto abort() -> Result<empty, bench::BenchError> {
        events->push(id * u64(10) + u64(2));
        if (fail_abort) return Err(bench::BenchError::Collector("abort failed"_Str));
        return Ok(empty {});
    }
};

TEST(BenchFramework, CollectorsWrapBatchesAndUnwindInReverseOrder) {
    Vec<u64> events;
    auto     collector =
        bench::collectors(TestCollector { &events, u64(1) }, TestCollector { &events, u64(2) });
    bench::BenchConfig config;
    config.epochs                 = usize(1);
    config.exact_epoch_iterations = Some(u64(3));
    config.warmup_iterations      = u64(1);
    bench::BasicBench<SuiteClock, decltype(collector)> engine(
        SuiteClock {}, rstd::move(collector), config);
    auto work = bench::batched_ref(
        [] {
            return u64();
        },
        [](u64& value) {
            ++value;
        },
        bench::BatchConfig { usize(2) });
    auto result = work.run(engine, "windows"_str);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(events.len(), usize(8));
    EXPECT_EQ(events[usize()], u64(10));
    EXPECT_EQ(events[usize(1)], u64(20));
    EXPECT_EQ(events[usize(2)], u64(21));
    EXPECT_EQ(events[usize(3)], u64(11));
    const auto& metrics = result->measurements()[usize()].metrics;
    ASSERT_EQ(metrics.len(), usize(2));
    EXPECT_EQ(metrics[usize()].value, u64(3));
    EXPECT_EQ(metrics[usize(1)].value, u64(3));
}

TEST(BenchFramework, CollectorStartAndAbortErrorsBothSurvive) {
    Vec<u64> events;
    auto     collector = bench::collectors(TestCollector { &events, u64(1), false, false, true },
                                           TestCollector { &events, u64(2), true });
    auto     started   = collector.begin();
    ASSERT_TRUE(started.is_err());
    auto error = bench::describe_error(started.unwrap_err());
    EXPECT_TRUE(error.as_str().contains("begin failed"_str));
    EXPECT_TRUE(error.as_str().contains("abort failed"_str));
    ASSERT_EQ(events.len(), usize(3));
    EXPECT_EQ(events[usize(2)], u64(12));
}

TEST(BenchFramework, MissingCounterEpochDoesNotUseFullIterationDenominator) {
    Vec<bench::EpochMeasurement> epochs;
    epochs.push(
        { rstd::time::Duration::from_nanos(u64(100)), u64(2), { .instructions = Some(u64(40)) } });
    epochs.push({ rstd::time::Duration::from_nanos(u64(200)), u64(2), {} });
    bench::RunConfig work;
    work.items_per_iteration = u64(3);
    work.bytes_per_iteration = u64(8);
    bench::BenchmarkResult result("partial"_Str,
                                  rstd::move(work),
                                  {},
                                  rstd::time::Duration::from_nanos(u64(1)),
                                  bench::CounterAvailability::Unavailable(i32(1)),
                                  rstd::move(epochs));
    auto                   summary = result.summary();
    EXPECT_TRUE(summary.instructions_per_unit.is_none());
    ASSERT_TRUE(summary.median_bytes_per_second.is_some());
    EXPECT_DOUBLE_EQ(summary.median_bytes_per_second->to_primitive(), 120000000.0);
}

TEST(BenchFramework, FallibleOperationStopsAndStillEndsCollector) {
    Vec<u64>           events;
    bench::BenchConfig config;
    config.epochs                 = usize(1);
    config.exact_epoch_iterations = Some(u64(5));
    bench::BasicBench<SuiteClock, TestCollector> engine(
        SuiteClock {}, TestCollector { &events, u64(1), false, true }, config);
    usize calls;
    auto  workload = bench::fallible([&]() -> Result<u64, String> {
        if (++calls == usize(2)) return Err("operation failed"_Str);
        return Ok(u64(1));
    });
    auto  result   = workload.run(engine, "fallible"_str);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(calls, usize(2));
    ASSERT_EQ(events.len(), usize(2));
    EXPECT_EQ(events[usize(1)], u64(11));
    auto error = bench::describe_error(result.unwrap_err());
    EXPECT_TRUE(error.as_str().contains("operation failed"_str));
    EXPECT_TRUE(error.as_str().contains("end failed"_str));
}

auto sample_report(rstd::time::Duration max_epoch_time =
                       rstd::time::Duration::from_millis(u64(100))) -> bench::RunReport {
    bench::BenchConfig config;
    config.epochs                 = usize(1);
    config.max_epoch_time         = max_epoch_time;
    config.exact_epoch_iterations = Some(u64(2));
    config.counter_mode           = bench::CounterMode::Disabled();
    bench::BasicBench<SuiteClock, bench::NoopCollector> engine(
        SuiteClock {}, bench::NoopCollector {}, config);
    auto measured   = engine.run("json"_str, [] {
        return u64(7);
    });
    auto descriptor = case_descriptor("json"_str);
    descriptor.parameters.push({ "large"_Str, bench::ParameterValue::Unsigned(u64::MAX) });
    bench::RunReport report;
    report.results.push({ rstd::move(descriptor),
                          bench::CaseOutcome::Passed(rstd::move(measured).unwrap()),
                          u64() });
    return report;
}

TEST(BenchFramework, JsonRoundTripRetainsIntegerPrecisionAndRejectsOldReports) {
    auto original = sample_report();
    auto encoded  = rstd_bench::report::encode(original);
    ASSERT_TRUE(encoded.is_ok());
    auto decoded = rstd_bench::report::decode(encoded->as_str());
    ASSERT_TRUE(decoded.is_ok());
    ASSERT_EQ(decoded->results.len(), usize(1));
    const auto& entry = decoded->results[usize()];
    EXPECT_EQ(entry.descriptor.parameters[usize()].value.as_Unsigned().value, u64::MAX);
    EXPECT_EQ(entry.repetition, u64());
    EXPECT_EQ(entry.outcome.as_Passed().measurement.measurements()[usize()].iterations, u64(2));
    EXPECT_TRUE(rstd_bench::report::decode("{\"format_version\":1}"_str).is_err());
    EXPECT_TRUE(rstd_bench::report::decode("{\"format_version\":2}"_str).is_err());
    EXPECT_TRUE(
        rstd_bench::report::decode("{\"format_version\":3,\"format_version\":3}"_str).is_err());
}

TEST(BenchRunner, JsonRangeDoesNotRestrictFrameworkDurations) {
    auto report = sample_report(rstd::time::Duration::from_secs(u64::MAX));
    EXPECT_TRUE(report.validate().is_ok());
    EXPECT_TRUE(rstd_bench::report::encode(report).is_err());
}

TEST(BenchFramework, ComparisonRejectsMissingEnvironmentAndWorkloadChanges) {
    auto before = sample_report();
    auto after  = sample_report();
    auto strict = bench::compare(before, after);
    ASSERT_EQ(strict.cases.len(), usize(1));
    EXPECT_TRUE(strict.cases[usize()].candidate_over_baseline.is_none());
    auto cross = bench::compare(before, after, bench::ComparisonMode::CrossEnvironment);
    ASSERT_TRUE(cross.cases[usize()].candidate_over_baseline.is_some());
    EXPECT_DOUBLE_EQ(cross.cases[usize()].candidate_over_baseline->to_primitive(), 1.0);
    after.results[usize()].descriptor.revision = "2"_Str;
    auto different = bench::compare(before, after, bench::ComparisonMode::CrossEnvironment);
    EXPECT_TRUE(different.cases[usize()].candidate_over_baseline.is_none());
}

TEST(BenchFramework, ReportsRejectInconsistentRepetitions) {
    auto report                       = sample_report();
    auto other                        = sample_report();
    other.results[usize()].repetition = u64(1);
    report.results.push(rstd::move(other.results[usize()]));
    ASSERT_TRUE(report.validate().is_ok());
    report.results[usize(1)].descriptor.implementation = "different-build"_Str;
    EXPECT_TRUE(report.validate().is_err());
    report.results[usize(1)].descriptor.implementation = "rstd"_Str;
    report.results[usize(1)].descriptor.revision       = "2"_Str;
    EXPECT_TRUE(report.validate().is_err());
}

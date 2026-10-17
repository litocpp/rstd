#include <rstd/test/gtest.hpp>
import rstd;
import rstd.bench;
using namespace rstd::prelude;
using namespace rstd::literals;
namespace bench = rstd::bench;

auto external_result(ref<str>    source     = "device"_str,
                     ref<str>    boundary   = "dispatch_batch"_str,
                     Option<f64> resolution = None()) -> bench::BenchmarkResult {
    bench::ExternalBench engine;
    usize                calls;
    bench::RunConfig     work;
    work.unit                = "dispatch"_Str;
    work.bytes_per_iteration = u64(8);
    work.quantities.push({ "flops"_Str, bench::WorkUnit::Flops, u64(20) });
    auto result = engine.run(
        "external"_str,
        [&](u64 iterations) -> Result<bench::EpochMeasurement, bench::BenchError> {
            EXPECT_EQ(iterations, u64(2));
            auto elapsed =
                bench::ElapsedTime::from_nanoseconds(++calls == usize(1) ? f64(2.5) : f64(7.5));
            return Ok(bench::EpochMeasurement { *elapsed, iterations, {}, {} });
        },
        { usize(2), u64(2) },
        { String::make(source), String::make(boundary), resolution },
        rstd::move(work));
    EXPECT_TRUE(result.is_ok());
    EXPECT_EQ(calls, usize(2));
    return rstd::move(result).unwrap();
}
auto external_report(ref<str> source = "device"_str, ref<str> boundary = "dispatch_batch"_str)
    -> bench::RunReport {
    bench::RunReport report;
    report.results.push({ { "external"_Str, "test"_Str, "1"_Str, "rstd"_Str },
                          bench::CaseOutcome::Passed(external_result(source, boundary)),
                          u64() });
    return report;
}
TEST(BenchExternal, PreservesFractionalEpochsAndNormalizesOnce) {
    auto result = external_result();
    ASSERT_TRUE(result.validate().is_ok());
    EXPECT_TRUE(result.timing().resolution_ns.is_none());
    EXPECT_EQ(result.measurements()[usize()].elapsed.nanoseconds(), f64(2.5));
    auto summary = result.summary();
    EXPECT_EQ(summary.median_ns_per_unit, f64(2.5));
    EXPECT_EQ(summary.minimum_ns_per_unit, f64(1.25));
    EXPECT_EQ(summary.maximum_ns_per_unit, f64(3.75));
    EXPECT_EQ(summary.total_elapsed_ns, f64(10));
    EXPECT_EQ(summary.total_iterations, u64(4));
    EXPECT_EQ(*summary.rate("flops"_str), f64(8e9));
    EXPECT_EQ(*summary.median_bytes_per_second, f64(3.2e9));
}
TEST(BenchExternal, RejectsInvalidSamplesAndStopsOnFirstFailure) {
    for (auto value :
         array<double, 5> { 0.0, -1.0, __builtin_inf(), __builtin_nan(""), 9007199254740994.0 })
        EXPECT_TRUE(bench::ElapsedTime::from_nanoseconds(f64(value)).is_err());
    bench::ElapsedTime huge(rstd::time::Duration::from_secs(u64(10000000)));
    EXPECT_FALSE(huge.valid());
    bench::ExternalBench engine;
    usize                calls;
    auto measure = [&](u64 n) -> Result<bench::EpochMeasurement, bench::BenchError> {
        ++calls;
        if (calls == usize(2)) return Err(bench::BenchError::Operation("device failed"_Str));
        return Ok(bench::EpochMeasurement { rstd::time::Duration::from_nanos(u64(5)), n, {}, {} });
    };
    auto source = [] {
        return bench::TimingDescriptor { "device"_Str, "batch"_Str, None() };
    };
    EXPECT_TRUE(engine.run("bad"_str, measure, { usize(0), u64(2) }, source()).is_err());
    EXPECT_TRUE(engine.run("bad"_str, measure, { usize(2), u64(0) }, source()).is_err());
    EXPECT_TRUE(engine
                    .run("bad"_str,
                         measure,
                         { usize(2), u64(1) },
                         { "device"_Str, "batch"_Str, Some(f64(0)) })
                    .is_err());
    EXPECT_TRUE(engine.run(""_str, measure, { usize(2), u64(1) }, source()).is_err());
    EXPECT_EQ(calls, usize());
    auto failed = engine.run("bad"_str, measure, { usize(4), u64(2) }, source());
    ASSERT_TRUE(failed.is_err());
    EXPECT_EQ(calls, usize(2));
    EXPECT_EQ(bench::describe_error(failed.unwrap_err()), "device failed"_str);
    auto mismatch = [](u64) -> Result<bench::EpochMeasurement, bench::BenchError> {
        return Ok(
            bench::EpochMeasurement { rstd::time::Duration::from_nanos(u64(5)), u64(1), {}, {} });
    };
    EXPECT_TRUE(engine.run("bad"_str, mismatch, { usize(1), u64(2) }, source()).is_err());
    auto zero = [](u64 n) -> Result<bench::EpochMeasurement, bench::BenchError> {
        return Ok(bench::EpochMeasurement { {}, n, {}, {} });
    };
    EXPECT_TRUE(engine.run("bad"_str, zero, { usize(1), u64(2) }, source()).is_err());
    bench::RunConfig work;
    work.quantities.push({ "overflow"_Str, bench::WorkUnit::Count, u64::MAX });
    EXPECT_TRUE(
        engine.run("bad"_str, mismatch, { usize(2), u64(1) }, source(), rstd::move(work)).is_err());
}
TEST(BenchExternal, UsesSessionCleanupAndPropagatesErrors) {
    usize                                   finished;
    bench::BasicSuite<bench::ExternalBench> suite;
    auto                                    factory = bench::factory([&] {
        auto work = bench::externally_timed(
            [](u64) -> Result<bench::EpochMeasurement, bench::BenchError> {
                return Err(bench::BenchError::Operation("measurement failed"_Str));
            },
            { usize(1), u64(2) },
            { "device"_Str, "batch"_Str, None() });
        auto session = bench::session(
            rstd::move(work),
            []() -> Result<empty, String> {
                return Ok(empty {});
            },
            [&]() -> Result<empty, String> {
                ++finished;
                return Err("cleanup failed"_Str);
            });
        return Result<decltype(session), String>(Ok(rstd::move(session)));
    });
    ASSERT_TRUE(suite.add({ "external"_Str, "test"_Str, "1"_Str, "rstd"_Str }, rstd::move(factory))
                    .is_ok());
    bench::ExternalBench engine;
    auto                 outcome = suite.execute(usize(), engine);
    ASSERT_TRUE(outcome.is_Failed());
    EXPECT_EQ(finished, usize(1));
    EXPECT_EQ(outcome.as_Failed().errors.len(), usize(2));
}
TEST(BenchExternal, PublicReportRoundtripAndComparisonBoundaries) {
    auto report  = external_report();
    auto encoded = bench::report::encode(report);
    ASSERT_TRUE(encoded.is_ok());
    EXPECT_TRUE(encoded->as_str().contains("\"format_version\":4"_str));
    auto decoded = bench::report::decode(encoded->as_str());
    ASSERT_TRUE(decoded.is_ok());
    const auto& measured = decoded->results[usize()].outcome.as_Passed().measurement;
    EXPECT_TRUE(measured.timing().resolution_ns.is_none());
    EXPECT_EQ(measured.measurements()[usize()].elapsed.nanoseconds(), f64(2.5));
    EXPECT_EQ(*measured.summary().rate("flops"_str), f64(8e9));
    bench::ConsoleRenderer renderer({ .verbose = true });
    auto                   text = renderer.render(decoded->results[usize()]);
    EXPECT_TRUE(text.as_str().contains("unknown"_str));
    EXPECT_TRUE(text.as_str().contains("FLOP/s"_str));
    EXPECT_TRUE(text.as_str().contains("1 dispatch"_str));
    EXPECT_TRUE(text.as_str().contains("elapsed [ns]"_str));
    auto host     = external_report("host_monotonic"_str, "launch_and_wait"_str);
    auto compared = bench::compare(report, host, bench::ComparisonMode::CrossEnvironment);
    ASSERT_EQ(compared.cases.len(), usize(1));
    EXPECT_TRUE(compared.cases[usize()].candidate_over_baseline.is_none());
    auto different_boundary = external_result("device"_str, "other"_str);
    EXPECT_FALSE(measured.same_measurement_config(different_boundary));
    EXPECT_TRUE(bench::report::decode("{\"format_version\":3}"_str).is_err());
}

TEST(BenchExternal, ResolutionIsMetadataAndVariableIterationsNormalizePerEpoch) {
    auto unknown = external_result();
    auto known   = external_result("device"_str, "dispatch_batch"_str, Some(f64(0.5)));
    EXPECT_TRUE(unknown.same_measurement_config(known));
    bench::RunReport report;
    report.results.push({ { "external"_Str, "test"_Str, "1"_Str, "rstd"_Str },
                          bench::CaseOutcome::Passed(rstd::move(known)),
                          u64() });
    auto encoded = bench::report::encode(report);
    ASSERT_TRUE(encoded.is_ok());
    auto decoded = bench::report::decode(encoded->as_str());
    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded->results[usize()].outcome.as_Passed().measurement.timing().resolution_ns,
              Some(f64(0.5)));
    bench::BenchConfig config;
    config.epochs = usize(2);
    bench::RunConfig work;
    work.batch = f64(2);
    work.quantities.push({ "flops"_Str, bench::WorkUnit::Flops, u64(20) });
    Vec<bench::EpochMeasurement> epochs;
    epochs.push({ bench::ElapsedTime::from_nanoseconds(f64(3)).unwrap(), u64(1), {}, {} });
    epochs.push({ bench::ElapsedTime::from_nanoseconds(f64(18)).unwrap(), u64(3), {}, {} });
    bench::BenchmarkResult result(
        "variable"_Str,
        rstd::move(work),
        config,
        bench::TimingDescriptor { "device"_Str, "dispatch_batch"_Str, None() },
        bench::CounterAvailability::Disabled(),
        rstd::move(epochs),
        bench::MeasurementScope::External,
        None(),
        "none/v1"_Str);
    ASSERT_TRUE(result.validate().is_ok());
    auto summary = result.summary();
    EXPECT_EQ(summary.median_ns_per_unit, f64(2.25));
    EXPECT_EQ(summary.total_elapsed_ns, f64(21));
    EXPECT_EQ(summary.total_iterations, u64(4));
    EXPECT_EQ(*summary.rate("flops"_str), f64(20.0 / 4.5 * 1e9));
}

TEST(BenchExternal, RejectsElapsedSumOverflowAndNormalizationUnderflow) {
    bench::ExternalBench engine;
    auto                 source = [] {
        return bench::TimingDescriptor { "device"_Str, "batch"_Str, None() };
    };
    auto large = [](u64 n) -> Result<bench::EpochMeasurement, bench::BenchError> {
        return Ok(bench::EpochMeasurement {
            bench::ElapsedTime::from_nanoseconds(f64(9007199254740992.0)).unwrap(), n, {}, {} });
    };
    EXPECT_TRUE(engine.run("overflow"_str, large, { usize(2), u64(1) }, source()).is_err());
    auto small = [](u64 n) -> Result<bench::EpochMeasurement, bench::BenchError> {
        return Ok(bench::EpochMeasurement {
            bench::ElapsedTime::from_nanoseconds(f64(1e-300)).unwrap(), n, {}, {} });
    };
    bench::RunConfig work;
    work.batch = f64(1e100);
    EXPECT_TRUE(engine.run("underflow"_str, small, { usize(1), u64(1) }, source(), rstd::move(work))
                    .is_err());
}

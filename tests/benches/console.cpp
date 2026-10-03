#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;
import rstd.bench;
import rstd_benches;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd_bench;
namespace bench = rstd::bench;

TEST(BenchConsole, ScalesUnitsAndLimitsPrecision) {
    EXPECT_EQ(console::number(f64()), "0"_str);
    EXPECT_EQ(console::number(f64(28.514)), "28.51"_str);
    EXPECT_EQ(console::time(f64(999.96)), "1.000 us"_str);
    EXPECT_EQ(console::time(f64(1e6)), "1.000 ms"_str);
    EXPECT_EQ(console::time(f64(1e9)), "1.000 s"_str);
    EXPECT_EQ(console::time(f64(1e-8)), "1.000e-8 ns"_str);
    EXPECT_EQ(console::bytes(f64(256)), "256 B"_str);
    EXPECT_EQ(console::bytes(f64(1024)), "1.000 KiB"_str);
    EXPECT_EQ(console::bytes(f64(1023.6)), "0.9996 KiB"_str);
    EXPECT_EQ(console::byte_rate(f64(999960000)), "1.000 GB/s"_str);
    EXPECT_EQ(console::item_rate(f64(47505938)), "47.51M"_str);
    EXPECT_EQ(console::exact(bench::DiagnosticValue::Count(u64::MAX)), "18446744073709551615"_str);
}

auto console_timing(ref<str> id = "test.case"_str, u64 items = u64(), u64 bytes = u64())
    -> bench::RunResult {
    bench::BenchConfig config;
    config.epochs                 = usize(1);
    config.exact_epoch_iterations = Some(u64(10));
    Vec<bench::EpochMeasurement> epochs;
    epochs.push({ rstd::time::Duration::from_nanos(u64(1000)), u64(10), {} });
    bench::RunConfig       work { "element"_Str, f64(2), items, bytes };
    bench::BenchmarkResult measurement(String::make(id),
                                       rstd::move(work),
                                       config,
                                       rstd::time::Duration::from_nanos(u64(1)),
                                       bench::CounterAvailability::Disabled(),
                                       rstd::move(epochs),
                                       bench::MeasurementScope::Repeated,
                                       None(),
                                       "noop/v1"_Str);
    return { { String::make(id), "test"_Str, "1"_Str, "rstd"_Str },
             bench::CaseOutcome::Passed(rstd::move(measurement)),
             u64() };
}

TEST(BenchConsole, TimingColumnsAreStreamingAndKeepPerOperationMeaning) {
    ConsoleRenderer renderer;
    renderer.include("test.case"_str);
    auto first = renderer.render(console_timing());
    EXPECT_EQ(
        first.as_str(),
        "case             median/op\n--------------------------\ntest.case         100.0 ns\n"_str);
    auto second = renderer.render(console_timing("test.case"_str, u64(2), u64(256)));
    EXPECT_TRUE(second.as_str().starts_with("case"_str));
    EXPECT_TRUE(second.as_str().contains("items/s"_str));
    EXPECT_TRUE(second.as_str().contains("bytes/s"_str));
    EXPECT_TRUE(second.as_str().contains("20.00M"_str));
    EXPECT_TRUE(second.as_str().contains("2.560 GB/s"_str));
    auto third = renderer.render(console_timing());
    EXPECT_FALSE(third.as_str().contains("median/op"_str));
    EXPECT_TRUE(third.as_str().contains("-"_str));
    EXPECT_FALSE(first.as_str().contains("repeat"_str));
    ConsoleRenderer verbose({ true, u64(2) });
    auto            detail = verbose.render(console_timing());
    EXPECT_TRUE(detail.as_str().contains("1/2"_str));
    EXPECT_TRUE(detail.as_str().contains("units/op: 2; unit: element"_str));
    EXPECT_TRUE(detail.as_str().contains("median: 100 ns/op"_str));
    EXPECT_TRUE(detail.as_str().contains("operations: 10"_str));
}

TEST(BenchConsole, NamesAndErrorsCannotBreakRows) {
    EXPECT_EQ(console::escaped("中\n\x1b[31m"_str), "中\\u{a}\\u{1b}[31m"_str);
    auto            long_name = "case.name.that.is.longer.than.the.maximum.console.name.column"_str;
    ConsoleRenderer renderer;
    renderer.include(long_name);
    auto output = renderer.render(console_timing(long_name));
    EXPECT_TRUE(output.as_str().contains(rstd::format("{}\n", long_name).as_str()));
    auto unicode = renderer.render(console_timing("中文"_str));
    EXPECT_TRUE(unicode.as_str().starts_with("中文\n"_str));
    auto failed    = console_timing("test\ncase"_str);
    failed.outcome = bench::failed("bad\ninput"_Str);
    auto failure   = renderer.render(failed);
    EXPECT_TRUE(failure.as_str().contains("test\\u{a}case"_str));
    EXPECT_TRUE(failure.as_str().contains("failed\n  bad\\u{a}input\n"_str));
    failed.outcome = bench::CaseOutcome::Skipped("no diagnostic"_Str);
    EXPECT_TRUE(renderer.render(failed).as_str().contains("skipped\n  no diagnostic\n"_str));
}

auto allocation_measurement() -> bench::DiagnosticMeasurement {
    bench::DiagnosticMeasurement result;
    result.iterations               = u64(2);
    result.work.items_per_iteration = u64(64);
    result.allocation_unavailable   = String();
    rstd::alloc::AllocationStats before { .allocations     = u64(1),
                                          .requested_bytes = u64(128),
                                          .live_bytes      = u64(128),
                                          .peak_live_bytes = u64(128) };
    auto                         after = before;
    after.allocations                  = u64(2);
    after.requested_bytes              = u64(384);
    after.peak_live_bytes              = u64(384);
    auto lifetime                      = after;
    lifetime.live_bytes                = u64();
    lifetime.deallocations             = u64(2);
    result.allocations =
        Some(bench::AllocationDiagnostic { { before, after, u64(384) }, lifetime });
    return result;
}

TEST(BenchDiagnostic, AllocationSummaryKeepsFractionsAndSnapshotBytes) {
    auto measured = allocation_measurement();
    auto summary  = measured.allocation_summary();
    ASSERT_TRUE(summary.allocations_per_op.is_Ratio());
    EXPECT_EQ(summary.allocations_per_op.as_Ratio().numerator, u64(1));
    EXPECT_EQ(summary.allocations_per_op.as_Ratio().denominator, u64(2));
    EXPECT_EQ(summary.bytes_per_op.as_Ratio().numerator, u64(256));
    EXPECT_EQ(summary.peak_live.as_Count().value, u64(384));
    EXPECT_EQ(summary.before_live.as_Count().value, u64(128));
    EXPECT_EQ(summary.cleanup_live.as_Count().value, u64());
    measured.allocations->operation.before.requested_bytes = u64();
    measured.allocations->operation.after.requested_bytes  = u64::MAX;
    measured.allocations->lifetime.requested_bytes         = u64::MAX;
    EXPECT_EQ(measured.allocation_summary().bytes_per_op.as_Ratio().numerator, u64::MAX);
    measured.allocations->operation.after.overflow = true;
    EXPECT_TRUE(measured.allocation_summary().allocations_per_op.is_Overflow());
    measured.allocations            = None();
    measured.allocation_unavailable = "allocator not injected"_Str;
    EXPECT_EQ(measured.allocation_summary().peak_live.as_Unavailable().reason,
              "allocator not injected"_str);
    measured            = allocation_measurement();
    measured.iterations = u64();
    EXPECT_TRUE(measured.allocation_summary().bytes_per_op.is_Unavailable());
    measured.iterations                               = u64(2);
    measured.allocations->operation.after.allocations = u64();
    EXPECT_TRUE(measured.allocation_summary().allocations.is_Unavailable());
}

TEST(BenchConsole, DiagnosticRowsKeepAvailabilityAndExactDetails) {
    auto measured = allocation_measurement();
    measured.metrics.push({ "capacity_utilization"_Str,
                            bench::DiagnosticUnit::Ratio,
                            bench::MetricDirection::Neutral,
                            "prepared-map"_Str,
                            "HashMap::len/capacity"_Str,
                            bench::DiagnosticValue::Ratio(u64(100), u64(112)) });
    auto result    = console_timing();
    result.outcome = bench::CaseOutcome::Diagnosed(rstd::move(measured));
    ConsoleRenderer renderer({ true, u64(1) });
    renderer.include(result.descriptor.id.as_str());
    auto output = renderer.render(result);
    EXPECT_TRUE(output.as_str().contains("0.5000"_str));
    EXPECT_TRUE(output.as_str().contains("128 B"_str));
    EXPECT_TRUE(output.as_str().contains("384 B"_str));
    EXPECT_TRUE(output.as_str().contains("allocs/op: 1/2"_str));
    EXPECT_TRUE(
        output.as_str().contains("89.29% [prepared-map; HashMap::len/capacity; raw=100/112]"_str));
    EXPECT_FALSE(output.as_str().contains("median/op"_str));
    result.outcome.as_Diagnosed().measurement.allocations            = None();
    result.outcome.as_Diagnosed().measurement.allocation_unavailable = "no allocator"_Str;
    EXPECT_TRUE(
        renderer.render(result).as_str().contains("allocations unavailable: no allocator"_str));
    result.outcome.as_Diagnosed().measurement = allocation_measurement();
    result.outcome.as_Diagnosed().measurement.allocations->operation.after.overflow = true;
    EXPECT_TRUE(renderer.render(result).as_str().contains("overflow"_str));
}

TEST(BenchConsole, ComparisonUsesCommonUnitsAndPreservesRejectionReasons) {
    bench::ComparisonReport report { .mode = bench::ComparisonMode::CrossEnvironment };
    report.environment_differences.push("profile"_Str);
    report.cases.push(
        { "test.case"_Str, Some(f64(999)), Some(f64(1001)), Some(f64(1001.0 / 999.0)), {} });
    bench::CaseComparison rejected { .id = "test.other"_Str };
    rejected.reasons.push("quick runs are not performance baselines"_Str);
    report.cases.push(rstd::move(rejected));
    auto output = console::comparison(report, true);
    EXPECT_TRUE(output.as_str().contains("descriptive only"_str));
    EXPECT_TRUE(output.as_str().contains("0.9990 us"_str));
    EXPECT_TRUE(output.as_str().contains("1.001 us"_str));
    EXPECT_TRUE(output.as_str().contains("+0.2002%"_str));
    EXPECT_TRUE(output.as_str().contains("not comparable"_str));
    EXPECT_TRUE(output.as_str().contains("quick runs are not performance baselines"_str));
    EXPECT_TRUE(output.as_str().contains("baseline: 999 ns/op"_str));
}

struct RejectedConsole {};
template<>
struct rstd::Impl<rstd::io::Write, RejectedConsole> : ImplBase<RejectedConsole> {
    auto write(slice<u8>) -> rstd::io::Result<usize> {
        return Err(rstd::io::error::Error::from_kind(
            rstd::io::error::ErrorKind { rstd::io::error::ErrorKind::PermissionDenied }));
    }
    auto flush() -> rstd::io::Result<empty> { return Ok(empty {}); }
};

TEST(BenchConsole, ReporterWritesBeforeNextCaseAndPropagatesIoErrors) {
    rstd::io::Cursor<Vec<u8>> output(Vec<u8>::make());
    ConsoleReporter           reporter { output, ConsoleRenderer() };
    bench::Suite              suite;
    bool                      saw_first = false;
    for (usize i; i < usize(2); ++i) {
        auto id   = rstd::format("test.{}", i);
        auto name = id.clone();
        ASSERT_TRUE(suite
                        .add_function({ rstd::move(id), "test"_Str, "1"_Str, "rstd"_Str },
                                      [&, name = rstd::move(name), i](bench::Bench&, ref<str>) {
                                          if (i == usize(1)) saw_first = output.position() > u64();
                                          return console_timing(name.as_str()).outcome;
                                      })
                        .is_ok());
    }
    auto result = bench::Runner().run(suite, reporter);
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(saw_first);
    RejectedConsole rejected;
    ConsoleReporter failing { rejected, ConsoleRenderer() };
    auto            failure = bench::Runner().run(suite, failing);
    ASSERT_TRUE(failure.is_err());
    EXPECT_TRUE(failure.unwrap_err().as_str().contains("cannot write benchmark output"_str));
}

TEST(BenchConsole, VerboseParsingContextAndJsonAreIndependentOfRendering) {
    Vec<rstd::ffi::OsString> args;
    args.push(rstd::ffi::OsString::from("bench"_str));
    args.push(rstd::ffi::OsString::from("--verbose"_str));
    auto options = parse_options(rstd::move(args));
    ASSERT_TRUE(options.is_ok());
    EXPECT_TRUE(options->verbose);
    auto context = console::context(true, false, u64(2), false, {});
    EXPECT_TRUE(context.as_str().contains("smoke only"_str));
    EXPECT_TRUE(context.as_str().contains("profile: unknown"_str));
    EXPECT_TRUE(context.as_str().contains("same process"_str));
    bench::RunReport report;
    report.results.push(console_timing());
    auto before = rstd_bench::report::encode(report);
    ASSERT_TRUE(before.is_ok());
    auto rendered = ConsoleRenderer().render(report.results[usize()]);
    auto after    = rstd_bench::report::encode(report);
    ASSERT_TRUE(after.is_ok());
    EXPECT_EQ(before->as_str(), after->as_str());
    EXPECT_TRUE(rstd_bench::report::decode(after->as_str()).is_ok());
}

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

auto console_timing(ref<str>                 id         = "test.case"_str,
                    u64                      items      = u64(),
                    u64                      bytes      = u64(),
                    Vec<bench::WorkQuantity> quantities = {},
                    bench::TimingDescriptor  timing     = { "host_monotonic"_Str,
                                                            "operation_window"_Str,
                                                            Some(f64(1)) }) -> bench::RunResult {
    bench::BenchConfig config;
    config.epochs                 = usize(1);
    config.exact_epoch_iterations = Some(u64(10));
    Vec<bench::EpochMeasurement> epochs;
    epochs.push({ rstd::time::Duration::from_nanos(u64(1000)), u64(10), {} });
    bench::RunConfig       work { "element"_Str, f64(2), items, bytes, rstd::move(quantities) };
    bench::BenchmarkResult measurement(String::make(id),
                                       rstd::move(work),
                                       config,
                                       rstd::move(timing),
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
    EXPECT_TRUE(first.as_str().starts_with("source"_str));
    EXPECT_TRUE(first.as_str().contains("host_monotonic"_str));
    EXPECT_TRUE(first.as_str().contains("operation_window"_str));
    EXPECT_TRUE(first.as_str().contains("2 element"_str));
    EXPECT_TRUE(first.as_str().contains("median/op"_str));
    EXPECT_TRUE(first.as_str().contains("samples"_str));
    EXPECT_TRUE(first.as_str().contains("100.0 ns"_str));
    EXPECT_FALSE(first.as_str().contains("timing:"_str));
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

TEST(BenchConsole, CompletedResultsCanDisplayNamesWithoutChangingIdentity) {
    auto row            = console_timing("candidate.k4"_str);
    row.descriptor.name = "f32"_Str;
    ConsoleRenderer renderer({ .use_case_names = true });
    renderer.include("f32"_str);
    auto output = renderer.render(row);
    EXPECT_TRUE(output.as_str().contains("f32"_str));
    EXPECT_FALSE(output.as_str().contains("candidate.k4"_str));
    EXPECT_TRUE(output.as_str().contains("100.0 ns"_str));
    EXPECT_EQ(row.descriptor.id, "candidate.k4"_str);
    EXPECT_TRUE(row.validate().is_ok());
    ConsoleRenderer defaults;
    EXPECT_TRUE(defaults.render(row).as_str().contains("candidate.k4"_str));
    row.outcome = bench::CaseOutcome::Skipped("unsupported"_Str);
    EXPECT_TRUE(renderer.render(row).as_str().starts_with("f32"_str));
    row.outcome = bench::failed("bad output"_Str);
    EXPECT_TRUE(renderer.render(row).as_str().starts_with("f32"_str));
    row.descriptor.name = String();
    EXPECT_TRUE(renderer.render(row).as_str().contains("candidate.k4"_str));
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

TEST(BenchConsole, NamedRatesKeepZeroMissingAndUnitsDistinct) {
    Vec<bench::WorkQuantity> quantities;
    quantities.push({ "flops"_Str, bench::WorkUnit::Flops, u64() });
    quantities.push({ "read_bytes"_Str, bench::WorkUnit::Bytes, u64(64) });
    ConsoleRenderer renderer;
    auto            first =
        renderer.render(console_timing("copy"_str, u64(), u64(128), rstd::move(quantities)));
    EXPECT_TRUE(first.as_str().contains("flops [FLOP/s]"_str));
    EXPECT_TRUE(first.as_str().contains("read_bytes [B/s]"_str));
    EXPECT_TRUE(first.as_str().contains("              0"_str));
    EXPECT_TRUE(first.as_str().contains("640.0 MB/s"_str));
    EXPECT_FALSE(first.as_str().contains("read_bytes:"_str));
    auto missing = renderer.render(console_timing("latency"_str));
    EXPECT_FALSE(missing.as_str().contains("source"_str));
    EXPECT_FALSE(missing.as_str().contains("median/op"_str));
    EXPECT_TRUE(missing.as_str().contains("              -"_str));
    Vec<bench::WorkQuantity> changed;
    changed.push({ "flops"_Str, bench::WorkUnit::Count, u64(3) });
    auto other = renderer.render(console_timing("other"_str, u64(), u64(), rstd::move(changed)));
    EXPECT_TRUE(other.as_str().contains("flops [FLOP/s]"_str));
    EXPECT_TRUE(other.as_str().contains("flops [count/s]"_str));
    EXPECT_TRUE(other.as_str().contains("30.00M"_str));
}

TEST(BenchConsole, FixedTableKeepsColumnsAcrossRatesTimingAndFailures) {
    bench::ConsoleTable table { "Compute"_Str, {} };
    table.rates.push({ "flops"_Str, bench::WorkUnit::Flops, "flops [FLOP/s]"_Str });
    table.rates.push({ "integer_ops"_Str, bench::WorkUnit::Count, "integer_ops [OP/s]"_Str });
    ConsoleRenderer renderer(rstd::move(table));
    renderer.include("unsupported"_str);
    auto skipped    = console_timing("unsupported"_str);
    skipped.outcome = bench::CaseOutcome::Skipped("no\ncapability"_Str);
    auto first      = renderer.render(skipped);
    EXPECT_TRUE(first.as_str().contains("Compute\n"_str));
    EXPECT_TRUE(first.as_str().contains("flops [FLOP/s]"_str));
    EXPECT_TRUE(first.as_str().contains("integer_ops [OP/s]"_str));
    EXPECT_TRUE(first.as_str().contains("skipped"_str));
    EXPECT_TRUE(first.as_str().contains("no\\u{a}capability"_str));
    Vec<bench::WorkQuantity> flops;
    flops.push({ "flops"_Str, bench::WorkUnit::Flops, u64(2) });
    flops.push({ "read_bytes"_Str, bench::WorkUnit::Bytes, u64(64) });
    auto measured = console_timing("f32"_str, u64(), u64(64), rstd::move(flops));
    auto second   = renderer.render(measured);
    EXPECT_FALSE(second.as_str().contains("Compute"_str));
    EXPECT_FALSE(second.as_str().contains("time/op"_str));
    EXPECT_FALSE(second.as_str().contains("read_bytes"_str));
    EXPECT_FALSE(second.as_str().contains("bytes/s"_str));
    EXPECT_TRUE(second.as_str().contains("20.00M"_str));
    EXPECT_TRUE(second.as_str().contains("100.0 ns"_str));
    EXPECT_TRUE(second.as_str().contains("              -"_str));
    Vec<bench::WorkQuantity> integer;
    integer.push({ "integer_ops"_Str, bench::WorkUnit::Count, u64() });
    integer.push({ "flops"_Str, bench::WorkUnit::Count, u64(3) });
    auto third = renderer.render(console_timing("u32"_str,
                                                u64(),
                                                u64(),
                                                rstd::move(integer),
                                                { "device"_Str, "batch"_Str, Some(f64(10)) }));
    EXPECT_FALSE(third.as_str().contains("time/op"_str));
    EXPECT_FALSE(third.as_str().contains("30.00M"_str));
    EXPECT_TRUE(third.as_str().contains("              0"_str));
    EXPECT_TRUE(third.as_str().contains("device"_str));
    measured.outcome = bench::failed("bad\nresult"_Str);
    auto last        = renderer.render(measured);
    EXPECT_FALSE(last.as_str().contains("time/op"_str));
    EXPECT_TRUE(last.as_str().contains("failed"_str));
    EXPECT_TRUE(last.as_str().contains("bad\\u{a}result"_str));
}

TEST(BenchConsole, FixedTableSelectsBandwidthWithoutAlteringMeasurements) {
    bench::ConsoleTable table { "Bandwidth"_Str, {} };
    table.rates.push({ "payload"_Str, bench::WorkUnit::Bytes, "bandwidth [B/s]"_Str });
    ConsoleRenderer renderer(rstd::move(table));
    renderer.include("copy"_str);
    Vec<bench::WorkQuantity> quantities;
    quantities.push({ "payload"_Str, bench::WorkUnit::Bytes, u64(128) });
    quantities.push({ "read_bytes"_Str, bench::WorkUnit::Bytes, u64(128) });
    bench::RunReport report;
    report.results.push(console_timing("copy"_str, u64(), u64(128), rstd::move(quantities)));
    auto before = bench::report::encode(report);
    ASSERT_TRUE(before.is_ok());
    auto output = renderer.render(report.results[usize()]);
    EXPECT_TRUE(output.as_str().contains("1.280 GB/s"_str));
    EXPECT_FALSE(output.as_str().contains("read_bytes"_str));
    auto after = bench::report::encode(report);
    ASSERT_TRUE(after.is_ok());
    EXPECT_EQ(before->as_str(), after->as_str());
}

auto console_occurrences(ref<str> text, ref<str> needle) -> usize {
    usize count;
    while (auto split = text.split_once(needle)) {
        ++count;
        text = split->template get<1>();
    }
    return count;
}

TEST(BenchConsole, FixedVerboseKeepsOneRowAndOnlySelectedRateColumns) {
    bench::ConsoleTable table { "Bandwidth"_Str, {} };
    table.rates.push({ "payload"_Str, bench::WorkUnit::Bytes, "bandwidth [B/s]"_Str });
    ConsoleRenderer renderer(rstd::move(table), { .verbose = true, .use_case_names = true });
    renderer.include("copy"_str);
    Vec<bench::WorkQuantity> quantities;
    quantities.push({ "payload"_Str, bench::WorkUnit::Bytes, u64(128) });
    quantities.push({ "read_bytes"_Str, bench::WorkUnit::Bytes, u64(128) });
    auto row = console_timing("candidate.copy"_str, u64(), u64(128), rstd::move(quantities));
    row.descriptor.name = "copy"_Str;
    auto first          = renderer.render(row);
    auto second         = renderer.render(row);
    EXPECT_EQ(console_occurrences(first.as_str(), "copy"_str), usize(1));
    EXPECT_EQ(console_occurrences(second.as_str(), "copy"_str), usize(1));
    EXPECT_EQ(console_occurrences(first.as_str(), "time/op"_str), usize(1));
    EXPECT_FALSE(second.as_str().contains("time/op"_str));
    EXPECT_FALSE(first.as_str().contains("candidate.copy"_str));
    EXPECT_FALSE(first.as_str().contains("median/op"_str));
    EXPECT_FALSE(first.as_str().contains("read_bytes"_str));
    EXPECT_FALSE(second.as_str().contains("read_bytes"_str));
    EXPECT_TRUE(first.as_str().contains("1.280 GB/s"_str));
    EXPECT_TRUE(first.as_str().contains("epoch        elapsed [ns]"_str));
    EXPECT_TRUE(second.as_str().contains("operations: 10"_str));
    EXPECT_TRUE(first.as_str().contains("operation_window"_str));
    EXPECT_TRUE(first.as_str().contains("resolution"_str));
}

TEST(BenchConsole, FixedVerboseDoesNotRepeatFailureOrSkipReasons) {
    ConsoleRenderer renderer(bench::ConsoleTable { "Results"_Str, {} }, { .verbose = true });
    renderer.include("candidate"_str);
    auto row     = console_timing("candidate"_str);
    row.outcome  = bench::failed("bad\nresult"_Str);
    auto failure = renderer.render(row);
    EXPECT_EQ(console_occurrences(failure.as_str(), "candidate"_str), usize(1));
    EXPECT_EQ(console_occurrences(failure.as_str(), "failed"_str), usize(1));
    EXPECT_EQ(console_occurrences(failure.as_str(), "bad\\u{a}result"_str), usize(1));
    row.outcome = bench::CaseOutcome::Skipped("no\ncapability"_Str);
    auto skip   = renderer.render(row);
    EXPECT_EQ(console_occurrences(skip.as_str(), "candidate"_str), usize(1));
    EXPECT_EQ(console_occurrences(skip.as_str(), "skipped"_str), usize(1));
    EXPECT_EQ(console_occurrences(skip.as_str(), "no\\u{a}capability"_str), usize(1));
}

TEST(BenchConsole, FixedVerbosePreservesDiagnosticDetailsWithoutAnotherTable) {
    ConsoleRenderer renderer(bench::ConsoleTable { "Results"_Str, {} }, { .verbose = true });
    renderer.include("diagnostic.case"_str);
    auto row      = console_timing("diagnostic.case"_str);
    auto measured = allocation_measurement();
    measured.metrics.push({ "utilization"_Str,
                            bench::DiagnosticUnit::Ratio,
                            bench::MetricDirection::Neutral,
                            "map"_Str,
                            "len/capacity"_Str,
                            bench::DiagnosticValue::Ratio(u64(100), u64(112)) });
    row.outcome = bench::CaseOutcome::Diagnosed(rstd::move(measured));
    auto output = renderer.render(row);
    EXPECT_EQ(console_occurrences(output.as_str(), "diagnostic.case"_str), usize(1));
    EXPECT_TRUE(output.as_str().contains("untimed"_str));
    EXPECT_FALSE(output.as_str().contains("allocated/op"_str));
    EXPECT_TRUE(output.as_str().contains("allocs/op: 1/2"_str));
    EXPECT_TRUE(output.as_str().contains("89.29% [map; len/capacity; raw=100/112]"_str));
}

TEST(BenchConsole, HeterogeneousSummaryPreservesUnitsAndFailures) {
    Vec<bench::SummaryRow> rows;
    rows.push({ "f32-fma"_Str, bench::SummaryValue::Measured(f64(1234.56)), "GFLOP/s"_Str });
    rows.push({ "shader-copy"_Str, bench::SummaryValue::Measured(f64(98.7)), "GB/s"_Str });
    rows.push({ "roundtrip"_Str, bench::SummaryValue::Measured(f64(12.3)), "us"_Str });
    rows.push(
        { "matrix"_Str, bench::SummaryValue::Skipped("missing\ncapability"_Str), "GFLOP/s"_Str });
    rows.push({ "write"_Str, bench::SummaryValue::Failed("bad\noutput"_Str), "GB/s"_Str });
    auto result = console::summary(rows.as_slice());
    ASSERT_TRUE(result.is_ok());
    auto text = result->as_str();
    EXPECT_TRUE(text.contains("GFLOP/s"_str));
    EXPECT_TRUE(text.contains("GB/s"_str));
    EXPECT_TRUE(text.contains("us"_str));
    EXPECT_TRUE(text.contains("1235"_str));
    EXPECT_TRUE(text.contains("skipped: missing\\u{a}capability"_str));
    EXPECT_TRUE(text.contains("failed: bad\\u{a}output"_str));
    EXPECT_TRUE(text.starts_with("case"_str));
    rows[usize(0)].value = bench::SummaryValue::Measured(f64::NAN_);
    EXPECT_TRUE(console::summary(rows.as_slice()).is_err());
}

TEST(BenchConsole, SummaryStreamsCompletedRows) {
    bench::SummaryRenderer renderer;
    renderer.include("f32"_str, "GFLOP/s"_str);
    renderer.include("matrix-f16/f32"_str, "GFLOP/s"_str);
    auto header = renderer.header();
    EXPECT_TRUE(header.as_str().starts_with("case"_str));
    EXPECT_TRUE(renderer.header().is_empty());
    auto first =
        renderer.render({ "f32"_Str, bench::SummaryValue::Measured(f64(42)), "GFLOP/s"_Str });
    ASSERT_TRUE(first.is_ok());
    EXPECT_TRUE(first->as_str().starts_with("f32"_str));
    EXPECT_FALSE(first->as_str().contains("status"_str));
    auto second = renderer.render({ "matrix-f16/f32"_Str,
                                    bench::SummaryValue::Skipped("missing\ncapability"_Str),
                                    "GFLOP/s"_Str });
    ASSERT_TRUE(second.is_ok());
    EXPECT_TRUE(second->as_str().starts_with("matrix-f16/f32"_str));
    EXPECT_FALSE(second->as_str().contains("status"_str));
    EXPECT_TRUE(second->as_str().contains("missing\\u{a}capability"_str));
    auto invalid =
        renderer.render({ "bad"_Str, bench::SummaryValue::Measured(f64::NAN_), "us"_Str });
    EXPECT_TRUE(invalid.is_err());
}

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
    auto cases = Vec<BenchCase>::make();
    EXPECT_TRUE(select_cases(*parsed, cases.as_slice()).is_err());
}

TEST(BenchRunner, MeasurementAndCleanupFailuresBothSurvive) {
    auto config    = bench::BenchConfig {};
    config.epochs  = usize();
    auto calls     = usize();
    auto cleaned   = false;
    auto validated = false;
    auto outcome   = measure_case(
        "failed",
        rstd::move(config),
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

TEST(BenchRunner, FailedAndSkippedReportsContainNoPerformanceSamples) {
    auto options  = Options {};
    options.quick = true;
    auto results  = Vec<RunResult>::make();
    results.push(RunResult {
        { "test", "failed", 1, nullptr },
        complete_measurement(
            Err(bench::BenchError::CounterUnavailable(i32(42))), false, Err("join failed"_Str)) });
    results.push(RunResult { { "test", "skipped", 1, nullptr },
                             CaseRunResult::Skipped("unsupported backend"_Str) });
    auto rendered = rstd::json::to_string(report(options, results));
    EXPECT_TRUE(rendered.as_str().contains("\"format_version\":1"_str));
    EXPECT_TRUE(rendered.as_str().contains("\"status\":\"failed\""_str));
    EXPECT_TRUE(rendered.as_str().contains("\"status\":\"skipped\""_str));
    EXPECT_TRUE(rendered.as_str().contains("join failed"_str));
    EXPECT_TRUE(rendered.as_str().contains("42"_str));
    EXPECT_FALSE(rendered.as_str().contains("elapsed_ns"_str));
    EXPECT_FALSE(rendered.as_str().contains("median_ns_per_op"_str));
}

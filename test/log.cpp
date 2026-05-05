#include <gtest/gtest.h>

import rstd.log;
import rstd.core;
import rstd.alloc;

using namespace rstd::prelude;
using namespace rstd::log;

// ── Level / LevelFilter ───────────────────────────────────────────────────

TEST(LogLevel, Comparison) {
    EXPECT_TRUE(Level::Error <= LevelFilter::Warn);
    EXPECT_TRUE(Level::Warn <= LevelFilter::Info);
    EXPECT_TRUE(Level::Info <= LevelFilter::Debug);
    EXPECT_TRUE(Level::Debug <= LevelFilter::Trace);

    EXPECT_FALSE(Level::Trace <= LevelFilter::Debug);
    EXPECT_FALSE(Level::Debug <= LevelFilter::Info);
    EXPECT_FALSE(Level::Info <= LevelFilter::Warn);
    EXPECT_FALSE(Level::Warn <= LevelFilter::Error);
}

TEST(LogLevel, EqLevelFilter) {
    EXPECT_TRUE(Level::Error == LevelFilter::Error);
    EXPECT_TRUE(Level::Info  == LevelFilter::Info);
    EXPECT_TRUE(Level::Trace == LevelFilter::Trace);
    EXPECT_FALSE(Level::Debug == LevelFilter::Trace);
}

TEST(LogLevel, ToLevelFilter) {
    EXPECT_EQ(to_level_filter(Level::Error), LevelFilter::Error);
    EXPECT_EQ(to_level_filter(Level::Info),  LevelFilter::Info);
    EXPECT_EQ(to_level_filter(Level::Trace), LevelFilter::Trace);
}

TEST(LogLevel, ToLevel) {
    EXPECT_TRUE(to_level(LevelFilter::Off).is_none());
    EXPECT_EQ(to_level(LevelFilter::Error).unwrap(), Level::Error);
    EXPECT_EQ(to_level(LevelFilter::Trace).unwrap(), Level::Trace);
}

// ── parse ─────────────────────────────────────────────────────────────────

TEST(LogParse, LevelCaseInsensitive) {
    EXPECT_EQ(parse_level("error").unwrap(), Level::Error);
    EXPECT_EQ(parse_level("WARN").unwrap(),  Level::Warn);
    EXPECT_EQ(parse_level("Info").unwrap(),  Level::Info);
    EXPECT_EQ(parse_level("dEbUg").unwrap(), Level::Debug);
    EXPECT_EQ(parse_level("trace").unwrap(), Level::Trace);
    EXPECT_TRUE(parse_level("invalid").is_none());
}

TEST(LogParse, LevelFilterCaseInsensitive) {
    EXPECT_EQ(parse_level_filter("off").unwrap(),   LevelFilter::Off);
    EXPECT_EQ(parse_level_filter("ERROR").unwrap(), LevelFilter::Error);
    EXPECT_EQ(parse_level_filter("TRACE").unwrap(), LevelFilter::Trace);
    EXPECT_TRUE(parse_level_filter("bad").is_none());
}

// ── Metadata / Record ─────────────────────────────────────────────────────

TEST(LogRecord, MetadataBuilder) {
    auto m = MetadataBuilder()
        .set_level(Level::Debug)
        .set_target("test")
        .build();
    EXPECT_EQ(m.lvl(), Level::Debug);
    EXPECT_EQ(m.tgt().size(), 4u);
}

TEST(LogRecord, RecordBuilder) {
    auto rec = RecordBuilder()
        .set_level(Level::Error)
        .set_target("my_mod")
        .build();
    EXPECT_EQ(rec.lvl(), Level::Error);
    EXPECT_EQ(rec.target().size(), 6u);
    EXPECT_EQ(rec.file(), nullptr);
}

// ── Max level ─────────────────────────────────────────────────────────────

TEST(LogMaxLevel, DefaultIsOff) {
    // Note: this test may be affected by other tests that set the global level.
    // In isolation, the default should be Off (0).
    EXPECT_EQ(max_level(), LevelFilter::Off);
}

TEST(LogMaxLevel, SetAndGet) {
    auto saved = max_level();
    set_max_level(LevelFilter::Debug);
    EXPECT_EQ(max_level(), LevelFilter::Debug);
    set_max_level(saved);
}

// ── EnvLogger filter parsing ──────────────────────────────────────────────

TEST(LogEnvLogger, GlobalLevel) {
    EnvLogger logger("debug");
    EXPECT_EQ(logger.filter(), LevelFilter::Debug);
    EXPECT_TRUE(logger.enabled(Metadata(Level::Debug, ref<str>())));
    EXPECT_TRUE(logger.enabled(Metadata(Level::Info, ref<str>())));
    EXPECT_FALSE(logger.enabled(Metadata(Level::Trace, ref<str>())));
}

TEST(LogEnvLogger, TargetLevel) {
    EnvLogger logger("my_mod=trace");
    EXPECT_TRUE(logger.enabled(Metadata(Level::Trace, "my_mod")));
    EXPECT_TRUE(logger.enabled(Metadata(Level::Trace, "my_mod::sub")));
    EXPECT_FALSE(logger.enabled(Metadata(Level::Trace, "other")));
}

TEST(LogEnvLogger, MixedRules) {
    EnvLogger logger("debug, my_mod=warn");
    // global debug
    EXPECT_TRUE(logger.enabled(Metadata(Level::Debug, "other")));
    // my_mod restricted to warn
    EXPECT_TRUE(logger.enabled(Metadata(Level::Warn, "my_mod")));
    EXPECT_FALSE(logger.enabled(Metadata(Level::Info, "my_mod")));
}

TEST(LogEnvLogger, OffLevel) {
    EnvLogger logger("error,my_mod=off");
    EXPECT_FALSE(logger.enabled(Metadata(Level::Error, "my_mod")));
    EXPECT_TRUE(logger.enabled(Metadata(Level::Error, "other")));
}

TEST(LogEnvLogger, DefaultError) {
    // No rules set → default is Error
    EnvLogger logger("");
    EXPECT_TRUE(logger.enabled(Metadata(Level::Error, ref<str>())));
    EXPECT_FALSE(logger.enabled(Metadata(Level::Warn, ref<str>())));
}

// ── Global logger registration ────────────────────────────────────────────

TEST(LogGlobal, SetLoggerOnce) {
    // The global logger can only be set once per process.
    // Use static storage so the pointer remains valid for the process lifetime.
    static EnvLogger logger;
    bool ok = set_logger(logger);
    // We don't assert ok because another test may have already set it.
    (void)ok;
}

// ── Macros compile ────────────────────────────────────────────────────────

TEST(LogMacros, Compile) {
    // These should compile and not crash.
    // Since no logger is set (or it might be), they should be no-ops after level check.
    set_max_level(LevelFilter::Trace);
    error("error message: {}", "one");
    warn("warn message: {}", "two");
    info("info message: {}", "three");
    debug("debug message: {}", "four");
    trace("trace message: {}", "five");
}

TEST(LogMacros, NoArgs) {
    set_max_level(LevelFilter::Trace);
    error("no args error");
    info("no args info");
}

TEST(LogMacros, FilteredOut) {
    set_max_level(LevelFilter::Error);
    // These should be skipped due to level filter
    trace("should not appear");
    debug("should not appear");
    info("should not appear");
    warn("should not appear");
    // error is allowed
    error("allowed error");
}

TEST(LogMacros, TargetCompile) {
    set_max_level(LevelFilter::Trace);
    error("my_mod", "targeted error: {}", "e");
    warn("my_mod", "targeted warn: {}", "w");
    info("my_mod", "targeted info: {}", "i");
    debug("my_mod", "targeted debug: {}", "d");
    trace("my_mod", "targeted trace: {}", "t");
}

TEST(LogMacros, TargetNoArgs) {
    set_max_level(LevelFilter::Trace);
    error("my_mod", "targeted error no args");
    info("my_mod", "targeted info no args");
}

// ── Convenience macros (rstd_*) ───────────────────────────────────────────

#include <rstd/macro.hpp>

TEST(LogMacroHelpers, LevelFiltering) {
    set_max_level(LevelFilter::Warn);
    // These are above the max level, macro guard skips them entirely
    rstd_info("should not run");
    rstd_debug("should not run");
    rstd_trace("should not run");

    // These are at or below the max level, they execute
    rstd_error("error ok");
    rstd_warn("warn ok");
}

TEST(LogMacroHelpers, NoArgs) {
    set_max_level(LevelFilter::Trace);
    rstd_error("no arg error");
    rstd_warn("no arg warn");
    rstd_info("no arg info");
    rstd_debug("no arg debug");
    rstd_trace("no arg trace");
}

TEST(LogMacroHelpers, FormatArgs) {
    set_max_level(LevelFilter::Trace);
    rstd_error("fmt: {}", "e");
    rstd_warn("fmt: {}", "w");
    rstd_info("fmt: {}", "i");
    rstd_debug("fmt: {}", "d");
    rstd_trace("fmt: {}", "t");
}

TEST(LogMacroHelpers, LazyEvaluation) {
    set_max_level(LevelFilter::Error);

    bool called = false;
    auto side_effect = [&] { called = true; return 42; };

    // info is filtered out, side_effect should not be called
    rstd_info("val: {}", side_effect());
    EXPECT_FALSE(called);

    // error is allowed, side_effect should be called
    rstd_error("val: {}", side_effect());
    EXPECT_TRUE(called);
}

// ── Target-specific macros ────────────────────────────────────────────────

TEST(LogMacroHelpers, TargetMacrosCompile) {
    set_max_level(LevelFilter::Trace);
    rstd_error_t("my_mod", "targeted error: {}", "e");
    rstd_warn_t("my_mod", "targeted warn: {}", "w");
    rstd_info_t("my_mod", "targeted info: {}", "i");
    rstd_debug_t("my_mod", "targeted debug: {}", "d");
    rstd_trace_t("my_mod", "targeted trace: {}", "t");
}

TEST(LogMacroHelpers, TargetMacrosFiltered) {
    static EnvLogger logger("error,my_mod=off");
    (void)set_logger(logger);
    set_max_level(LevelFilter::Trace);

    // my_mod is off, these should be filtered
    rstd_error_t("my_mod", "should not appear");
    rstd_warn_t("my_mod", "should not appear");
    rstd_info_t("my_mod", "should not appear");

    // other targets are allowed at error
    rstd_error_t("other", "other error ok");
}

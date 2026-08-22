#include <rstd/test/gtest.hpp>

import rstd.log;
import rstd;
import rstd.core;
import rstd.alloc;
import rstd.tests.log_module_check;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::log;

namespace
{

class MaxLevelGuard {
    LevelFilter saved_;

public:
    explicit MaxLevelGuard(LevelFilter value): saved_(max_level()) { set_max_level(value); }
    ~MaxLevelGuard() { set_max_level(saved_); }
};

} // namespace

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
    EXPECT_TRUE(Level::Info == LevelFilter::Info);
    EXPECT_TRUE(Level::Trace == LevelFilter::Trace);
    EXPECT_FALSE(Level::Debug == LevelFilter::Trace);
}

TEST(LogLevel, ToLevelFilter) {
    EXPECT_EQ(to_level_filter(Level::Error), LevelFilter::Error);
    EXPECT_EQ(to_level_filter(Level::Info), LevelFilter::Info);
    EXPECT_EQ(to_level_filter(Level::Trace), LevelFilter::Trace);
}

TEST(LogLevel, ToLevel) {
    EXPECT_TRUE(to_level(LevelFilter::Off).is_none());
    EXPECT_EQ(to_level(LevelFilter::Error).unwrap(), Level::Error);
    EXPECT_EQ(to_level(LevelFilter::Trace).unwrap(), Level::Trace);
}

// ── parse ─────────────────────────────────────────────────────────────────

TEST(LogParse, LevelCaseInsensitive) {
    EXPECT_EQ(parse_level("error"_str).unwrap(), Level::Error);
    EXPECT_EQ(parse_level("WARN"_str).unwrap(), Level::Warn);
    EXPECT_EQ(parse_level("Info"_str).unwrap(), Level::Info);
    EXPECT_EQ(parse_level("dEbUg"_str).unwrap(), Level::Debug);
    EXPECT_EQ(parse_level("trace"_str).unwrap(), Level::Trace);
    EXPECT_TRUE(parse_level("invalid"_str).is_none());
}

TEST(LogParse, LevelFilterCaseInsensitive) {
    EXPECT_EQ(parse_level_filter("off"_str).unwrap(), LevelFilter::Off);
    EXPECT_EQ(parse_level_filter("ERROR"_str).unwrap(), LevelFilter::Error);
    EXPECT_EQ(parse_level_filter("TRACE"_str).unwrap(), LevelFilter::Trace);
    EXPECT_TRUE(parse_level_filter("bad"_str).is_none());
}

// ── Metadata / Record ─────────────────────────────────────────────────────

TEST(LogRecord, MetadataBuilder) {
    auto m = MetadataBuilder().set_level(Level::Debug).set_target("test"_str).build();
    EXPECT_EQ(m.lvl(), Level::Debug);
    EXPECT_EQ(m.tgt().size(), rstd::usize(4));
}

TEST(LogRecord, RecordBuilder) {
    auto rec = RecordBuilder().set_level(Level::Error).set_target("my_mod"_str).build();
    EXPECT_EQ(rec.lvl(), Level::Error);
    EXPECT_EQ(rec.target().size(), rstd::usize(6));
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
    EnvLogger logger("debug"_str);
    EXPECT_EQ(logger.filter(), LevelFilter::Debug);
    EXPECT_TRUE(logger.enabled(Metadata(Level::Debug, ref<str>())));
    EXPECT_TRUE(logger.enabled(Metadata(Level::Info, ref<str>())));
    EXPECT_FALSE(logger.enabled(Metadata(Level::Trace, ref<str>())));
}

TEST(LogEnvLogger, TargetLevel) {
    EnvLogger logger("my_mod=trace"_str);
    EXPECT_TRUE(logger.enabled(Metadata(Level::Trace, "my_mod"_str)));
    EXPECT_TRUE(logger.enabled(Metadata(Level::Trace, "my_mod::sub"_str)));
    EXPECT_FALSE(logger.enabled(Metadata(Level::Trace, "other"_str)));
}

TEST(LogEnvLogger, MixedRules) {
    EnvLogger logger("debug, my_mod=warn"_str);
    // global debug
    EXPECT_TRUE(logger.enabled(Metadata(Level::Debug, "other"_str)));
    // my_mod restricted to warn
    EXPECT_TRUE(logger.enabled(Metadata(Level::Warn, "my_mod"_str)));
    EXPECT_FALSE(logger.enabled(Metadata(Level::Info, "my_mod"_str)));
}

TEST(LogEnvLogger, OffLevel) {
    EnvLogger logger("error,my_mod=off"_str);
    EXPECT_FALSE(logger.enabled(Metadata(Level::Error, "my_mod"_str)));
    EXPECT_TRUE(logger.enabled(Metadata(Level::Error, "other"_str)));
}

TEST(LogEnvLogger, DefaultError) {
    // No rules set → default is Error
    EnvLogger logger(""_str);
    EXPECT_TRUE(logger.enabled(Metadata(Level::Error, ref<str>())));
    EXPECT_FALSE(logger.enabled(Metadata(Level::Warn, ref<str>())));
}

// ── Global logger registration ────────────────────────────────────────────

TEST(LogGlobal, SetLoggerOnce) {
    // The global logger can only be set once per process.
    // Use static storage so the pointer remains valid for the process lifetime.
    static EnvLogger logger;
    bool             ok = set_logger(logger);
    // We don't assert ok because another test may have already set it.
    (void)ok;
}

// ── Macros compile ────────────────────────────────────────────────────────

TEST(LogMacros, Compile) {
    // These should compile and not crash.
    // Since no logger is set (or it might be), they should be no-ops after level check.
    auto max_level_guard = MaxLevelGuard(LevelFilter::Trace);
    error("error message: {}", "one");
    warn("warn message: {}", "two");
    info("info message: {}", "three");
    debug("debug message: {}", "four");
    trace("trace message: {}", "five");
}

TEST(LogMacros, NoArgs) {
    auto max_level_guard = MaxLevelGuard(LevelFilter::Trace);
    error("no args error");
    info("no args info");
}

TEST(LogMacros, FilteredOut) {
    auto max_level_guard = MaxLevelGuard(LevelFilter::Error);
    // These should be skipped due to level filter
    trace("should not appear");
    debug("should not appear");
    info("should not appear");
    warn("should not appear");
    // error is allowed
    error("allowed error");
}

TEST(LogMacros, TargetCompile) {
    auto max_level_guard = MaxLevelGuard(LevelFilter::Trace);
    error("my_mod", "targeted error: {}", "e");
    warn("my_mod", "targeted warn: {}", "w");
    info("my_mod", "targeted info: {}", "i");
    debug("my_mod", "targeted debug: {}", "d");
    trace("my_mod", "targeted trace: {}", "t");
}

TEST(LogMacros, TargetNoArgs) {
    auto max_level_guard = MaxLevelGuard(LevelFilter::Trace);
    error("my_mod", "targeted error no args");
    info("my_mod", "targeted info no args");
}

TEST(LogMacros, ExplicitModuleTargetCompiles) {
    static EnvLogger logger;
    (void)set_logger(logger);
    auto max_level_guard = MaxLevelGuard(LevelFilter::Trace);
    log_module_check::emit_with_target();
}

TEST(LogEnvLogger, OutputOmitsImplicitTarget) {
    EXPECT_DEATH(
        {
            EnvLogger logger("trace"_str);
            auto      arguments = rstd::fmt::Arguments::make("{}", "untargeted message");
            logger.log(Record { Metadata { Level::Info, ref<str>() },
                                arguments,
                                rstd::panic_::Location::from(source_location::current()) });
            rstd::process::exit(i32(1));
        },
        "INFO ] untargeted message");
}

TEST(LogEnvLogger, OutputUsesExplicitTarget) {
    EXPECT_DEATH(
        {
            EnvLogger logger("trace"_str);
            auto      arguments = rstd::fmt::Arguments::make("{}", "targeted message");
            logger.log(Record { Metadata { Level::Info, "explicit.target"_str },
                                arguments,
                                rstd::panic_::Location::from(source_location::current()) });
            rstd::process::exit(i32(1));
        },
        "INFO  explicit.target] targeted message");
}

// ── Convenience macros (rstd_*) ───────────────────────────────────────────

#include <rstd/macro.hpp>

TEST(LogMacroHelpers, LevelFiltering) {
    auto max_level_guard = MaxLevelGuard(LevelFilter::Warn);
    // These are above the max level, macro guard skips them entirely
    rstd_info("should not run");
    rstd_debug("should not run");
    rstd_trace("should not run");

    // These are at or below the max level, they execute
    rstd_error("error ok");
    rstd_warn("warn ok");
}

TEST(LogMacroHelpers, NoArgs) {
    auto max_level_guard = MaxLevelGuard(LevelFilter::Trace);
    rstd_error("no arg error");
    rstd_warn("no arg warn");
    rstd_info("no arg info");
    rstd_debug("no arg debug");
    rstd_trace("no arg trace");
}

TEST(LogMacroHelpers, FormatArgs) {
    auto max_level_guard = MaxLevelGuard(LevelFilter::Trace);
    rstd_error("fmt: {}", "e");
    rstd_warn("fmt: {}", "w");
    rstd_info("fmt: {}", "i");
    rstd_debug("fmt: {}", "d");
    rstd_trace("fmt: {}", "t");
}

TEST(LogMacroHelpers, LazyEvaluation) {
    static EnvLogger logger;
    (void)set_logger(logger);
    auto max_level_guard = MaxLevelGuard(LevelFilter::Error);

    bool called      = false;
    auto side_effect = [&] {
        called = true;
        return 42;
    };

    // info is filtered out, side_effect should not be called
    rstd_info("val: {}", side_effect());
    EXPECT_FALSE(called);

    // error is allowed, side_effect should be called
    rstd_error("val: {}", side_effect());
    EXPECT_TRUE(called);
}

// ── Target-specific macros ────────────────────────────────────────────────

TEST(LogMacroHelpers, TargetMacrosCompile) {
    auto max_level_guard = MaxLevelGuard(LevelFilter::Trace);
    rstd_error_t("my_mod"_str, "targeted error: {}", "e");
    rstd_warn_t("my_mod"_str, "targeted warn: {}", "w");
    rstd_info_t("my_mod"_str, "targeted info: {}", "i");
    rstd_debug_t("my_mod"_str, "targeted debug: {}", "d");
    rstd_trace_t("my_mod"_str, "targeted trace: {}", "t");
}

TEST(LogMacroHelpers, TargetMacrosFiltered) {
    static EnvLogger logger("error,my_mod=off"_str);
    (void)set_logger(logger);
    auto max_level_guard = MaxLevelGuard(LevelFilter::Trace);

    // my_mod is off, these should be filtered
    rstd_error_t("my_mod"_str, "should not appear");
    rstd_warn_t("my_mod"_str, "should not appear");
    rstd_info_t("my_mod"_str, "should not appear");

    // other targets are allowed at error
    rstd_error_t("other"_str, "other error ok");
}

#include <rstd/test/gtest.hpp>

#include <string>

import rstd;
import rstd.test;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{

auto fatal_helper(bool& reached) noexcept -> void {
    ASSERT_TRUE(false);
    reached = true;
}

auto nonfatal_helper(bool& reached) noexcept -> void {
    EXPECT_TRUE(false);
    reached = true;
}

auto fail_helper(bool& reached) noexcept -> void {
    FAIL() << "intentional failure " << 9;
    reached = true;
}

auto skip_helper(bool& reached) noexcept -> void {
    GTEST_SKIP() << "intentional skip " << 4;
    reached = true;
}

auto diagnostic_helper(bool& reached) noexcept -> void {
    SCOPED_TRACE(std::string("trace context"));
    EXPECT_TRUE(false) << std::string("nonfatal detail");
    ADD_FAILURE() << std::string("explicit detail");
    reached = true;
}

} // namespace

TEST(Macro, AllPassing) {
    auto value = 1;
    EXPECT_TRUE(value == 1);
    EXPECT_FALSE(value == 2);
    EXPECT_EQ(value++, 1);
    EXPECT_EQ(value, 2);
    EXPECT_NE(value, 3);
    EXPECT_GE(value, 2);
    EXPECT_GT(value, 1);
    EXPECT_LE(value, 2);
    EXPECT_LT(value, 3);
    ASSERT_TRUE(value == 2);
    ASSERT_FALSE(value == 3);
    ASSERT_EQ(value, 2);
    ASSERT_NE(value, 3);
    ASSERT_GE(value, 2);
    ASSERT_LT(value, 3);
    EXPECT_FLOAT_EQ(1.0f, 1.0f);
    EXPECT_DOUBLE_EQ(2.0, 2.0);
    EXPECT_NEAR(3.0, 3.01, 0.02);
    EXPECT_DEATH(rstd::process::abort(), "");
    EXPECT_DEATH(rstd::process::abort(), "");
    SUCCEED();
}

#define RSTD_MACRO_SUITE MacroExpanded
TEST(RSTD_MACRO_SUITE, SuiteNameExpands) {
    EXPECT_TRUE(true);
}

TEST(MacroControl, SkipWithMessage) {
    GTEST_SKIP() << "not available " << 7;
}

TEST(MacroControl, FatalStopsHelper) {
    auto context  = rstd::test::TestContext("MacroFixture"_str, "Fatal"_str, false);
    auto previous = rstd::test::replace_test_context(rstd::addressof(context));
    auto reached  = false;
    fatal_helper(reached);
    (void)rstd::test::replace_test_context(previous);
    EXPECT_FALSE(reached);
    EXPECT_EQ(context.failures(), rstd::usize(1));
    EXPECT_TRUE(context.fatal());
}

TEST(MacroControl, NonfatalContinues) {
    auto context  = rstd::test::TestContext("MacroFixture"_str, "Nonfatal"_str, false);
    auto previous = rstd::test::replace_test_context(rstd::addressof(context));
    auto reached  = false;
    nonfatal_helper(reached);
    (void)rstd::test::replace_test_context(previous);
    EXPECT_TRUE(reached);
    EXPECT_EQ(context.failures(), rstd::usize(1));
    EXPECT_FALSE(context.fatal());
}

TEST(MacroControl, FailStopsHelper) {
    auto context  = rstd::test::TestContext("MacroFixture"_str, "Fail"_str, false);
    auto previous = rstd::test::replace_test_context(rstd::addressof(context));
    auto reached  = false;
    fail_helper(reached);
    (void)rstd::test::replace_test_context(previous);
    EXPECT_FALSE(reached);
    EXPECT_EQ(context.failures(), rstd::usize(1));
    EXPECT_TRUE(context.fatal());
}

TEST(MacroControl, SkipStopsHelper) {
    auto context  = rstd::test::TestContext("MacroFixture"_str, "Skip"_str, false);
    auto previous = rstd::test::replace_test_context(rstd::addressof(context));
    auto reached  = false;
    skip_helper(reached);
    (void)rstd::test::replace_test_context(previous);
    EXPECT_FALSE(reached);
    EXPECT_TRUE(context.skipped());
    EXPECT_TRUE(context.skip_message().is_some());
}

TEST(MacroControl, DiagnosticsRemainNonfatal) {
    auto context  = rstd::test::TestContext("MacroFixture"_str, "Diagnostics"_str, false);
    auto previous = rstd::test::replace_test_context(rstd::addressof(context));
    auto reached  = false;
    diagnostic_helper(reached);
    (void)rstd::test::replace_test_context(previous);
    EXPECT_TRUE(reached);
    EXPECT_EQ(context.failures(), rstd::usize(2));
    EXPECT_FALSE(context.fatal());
}

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

struct FatalSetupFixtureCase {
    bool& body;
    bool& teardown;

    auto run_set_up() noexcept -> void { ASSERT_TRUE(false); }
    auto run_body() noexcept -> void { body = true; }
    auto run_tear_down() noexcept -> void { teardown = true; }
};

struct SkippedSetupFixtureCase {
    bool& body;
    bool& teardown;

    auto run_set_up() noexcept -> void { GTEST_SKIP(); }
    auto run_body() noexcept -> void { body = true; }
    auto run_tear_down() noexcept -> void { teardown = true; }
};

struct FatalBodyFixtureCase {
    bool& after_fatal;
    bool& teardown;

    auto run_set_up() noexcept -> void {}
    auto run_body() noexcept -> void {
        ASSERT_TRUE(false);
        after_fatal = true;
    }
    auto run_tear_down() noexcept -> void { teardown = true; }
};

} // namespace

class MacroFixture : public rstd::test::Test {
protected:
    bool setup_called_ {};
    bool body_called_ {};

    auto SetUp() noexcept -> void {
        EXPECT_FALSE(setup_called_);
        EXPECT_FALSE(body_called_);
        setup_called_ = true;
    }

    auto TearDown() noexcept -> void {
        EXPECT_TRUE(setup_called_);
        EXPECT_TRUE(body_called_);
    }
};

TEST_F(MacroFixture, FirstCaseGetsFreshFixture) {
    EXPECT_TRUE(setup_called_);
    EXPECT_FALSE(body_called_);
    body_called_ = true;
}

TEST_F(MacroFixture, SecondCaseGetsFreshFixture) {
    EXPECT_TRUE(setup_called_);
    EXPECT_FALSE(body_called_);
    body_called_ = true;
}

TEST(TempDir, OwnsMovesClosesAndKeepsDirectories) {
    auto automatic_path = rstd::path::PathBuf {};
    {
        auto automatic = rstd::test::TempDir::make();
        ASSERT_TRUE(automatic.is_ok());
        auto automatic_owner = rstd::move(automatic).unwrap();
        automatic_path       = rstd::path::PathBuf::from(automatic_owner.path());
        ASSERT_TRUE(rstd::fs::exists(automatic_path.as_path()).unwrap());
    }
    EXPECT_FALSE(rstd::fs::exists(automatic_path.as_path()).unwrap());

    auto created = rstd::test::TempDir::make();
    ASSERT_TRUE(created.is_ok());
    auto owner = rstd::move(created).unwrap();
    auto path  = rstd::path::PathBuf::from(owner.path());
    ASSERT_TRUE(rstd::fs::exists(path.as_path()).unwrap());

    auto moved = rstd::move(owner);
    ASSERT_TRUE(rstd::fs::exists(path.as_path()).unwrap());
    ASSERT_TRUE(moved.close().is_ok());
    EXPECT_FALSE(rstd::fs::exists(path.as_path()).unwrap());

    auto kept = rstd::test::TempDir::make();
    ASSERT_TRUE(kept.is_ok());
    auto kept_owner = rstd::move(kept).unwrap();
    auto kept_path  = kept_owner.keep();
    ASSERT_TRUE(rstd::fs::exists(kept_path.as_path()).unwrap());
    EXPECT_TRUE(rstd::fs::remove_dir_all(kept_path.as_path()).is_ok());
}

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

TEST(MacroControl, FixtureRunnerSkipsBodyAfterFatalSetupAndRunsTeardown) {
    auto context  = rstd::test::TestContext("MacroFixture"_str, "FatalSetup"_str, false);
    auto previous = rstd::test::replace_test_context(rstd::addressof(context));
    auto body     = false;
    auto teardown = false;
    rstd::test::gtest::run_fixture_case(FatalSetupFixtureCase { body, teardown });
    (void)rstd::test::replace_test_context(previous);
    EXPECT_FALSE(body);
    EXPECT_TRUE(teardown);
    EXPECT_TRUE(context.fatal());
}

TEST(MacroControl, FixtureRunnerSkipsBodyAfterSkippedSetupAndRunsTeardown) {
    auto context  = rstd::test::TestContext("MacroFixture"_str, "SkippedSetup"_str, false);
    auto previous = rstd::test::replace_test_context(rstd::addressof(context));
    auto body     = false;
    auto teardown = false;
    rstd::test::gtest::run_fixture_case(SkippedSetupFixtureCase { body, teardown });
    (void)rstd::test::replace_test_context(previous);
    EXPECT_FALSE(body);
    EXPECT_TRUE(teardown);
    EXPECT_TRUE(context.skipped());
}

TEST(MacroControl, FixtureRunnerRunsTeardownAfterFatalBody) {
    auto context  = rstd::test::TestContext("MacroFixture"_str, "FatalBody"_str, false);
    auto previous = rstd::test::replace_test_context(rstd::addressof(context));
    auto after    = false;
    auto teardown = false;
    rstd::test::gtest::run_fixture_case(FatalBodyFixtureCase { after, teardown });
    (void)rstd::test::replace_test_context(previous);
    EXPECT_FALSE(after);
    EXPECT_TRUE(teardown);
    EXPECT_TRUE(context.fatal());
}

import rstd;
#include <gtest/gtest.h>

using namespace rstd::prelude;

TEST(Env, VarNotFound) {
    auto val = rstd::env::var("RSTD_TEST_NONEXISTENT_VAR_12345");
    EXPECT_TRUE(val.is_none());
}

TEST(Env, SetAndGet) {
    rstd::env::set_var("RSTD_TEST_VAR", "hello");
    auto val = rstd::env::var("RSTD_TEST_VAR");
    ASSERT_TRUE(val.is_some());
    EXPECT_EQ("hello", val.unwrap());
}

TEST(Env, RemoveVar) {
    rstd::env::set_var("RSTD_TEST_REMOVE", "value");
    ASSERT_TRUE(rstd::env::var("RSTD_TEST_REMOVE").is_some());

    rstd::env::remove_var("RSTD_TEST_REMOVE");
    EXPECT_TRUE(rstd::env::var("RSTD_TEST_REMOVE").is_none());
}

TEST(Env, OverwriteVar) {
    rstd::env::set_var("RSTD_TEST_OVERWRITE", "first");
    rstd::env::set_var("RSTD_TEST_OVERWRITE", "second");
    auto val = rstd::env::var("RSTD_TEST_OVERWRITE");
    ASSERT_TRUE(val.is_some());
    EXPECT_EQ("second", val.unwrap());
}

TEST(Process, Id) {
    auto pid = rstd::process::id();
    EXPECT_GT(pid, 0u);
}

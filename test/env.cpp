#include <gtest/gtest.h>

import rstd;

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

TEST(Env, Args) {
    // glibc .init_array captured the real process argv; there is always argv[0].
    auto n = rstd::env::args().count();
    EXPECT_GE(n, 1u);

    auto first = rstd::env::args().next();
    ASSERT_TRUE(first.is_some());
    EXPECT_GT(first.unwrap().len(), 0u); // program path is non-empty
}

TEST(Env, ArgsManualInit) {
    const char* argv[] = { "prog", "--flag", "value" };
    rstd::env::args_init(3, argv);

    auto collected = rstd::env::args().collect<rstd::vec::Vec<rstd::string::String>>();
    ASSERT_EQ(collected.len(), 3u);
    EXPECT_EQ("prog", collected[0]);
    EXPECT_EQ("--flag", collected[1]);
    EXPECT_EQ("value", collected[2]);

    // args() is an ExactSize + DoubleEnded iterator
    auto it = rstd::env::args();
    EXPECT_EQ(rstd::as<rstd::iter::ExactSizeIterator>(it).len(), 3u);
    auto last = rstd::env::args().next_back();
    ASSERT_TRUE(last.is_some());
    EXPECT_EQ("value", last.unwrap());
}

TEST(Process, Id) {
    auto pid = rstd::process::id();
    EXPECT_GT(pid, 0u);
}

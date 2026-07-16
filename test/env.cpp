#include <gtest/gtest.h>

import rstd;

using namespace rstd::prelude;

static void iterate_invalid_unicode_arg() {
    const char  invalid[] = { 'v', static_cast<char>(0xff), '\0' };
    const char* argv[]    = { "prog", invalid };
    rstd::env::args_init(2, argv);

    auto args = rstd::env::args();
    (void)args.next();
    (void)args.next();
}

TEST(Env, VarNotFound) {
    auto val = rstd::env::var("RSTD_TEST_NONEXISTENT_VAR_12345");
    EXPECT_TRUE(val.is_none());
}

TEST(Env, TempDir) {
    auto path = rstd::env::temp_dir();
    EXPECT_FALSE(path.is_empty());
}

TEST(Env, SetAndGet) {
    rstd::env::set_var("RSTD_TEST_VAR", "hello");
    auto val = rstd::env::var("RSTD_TEST_VAR");
    ASSERT_TRUE(val.is_some());
    EXPECT_EQ("hello", val.unwrap());
}

TEST(Env, VarOsPreservesInvalidUnicode) {
    const char invalid[] = { 'v', static_cast<char>(0xff), '\0' };
    rstd::env::set_var("RSTD_TEST_VAR_OS", invalid);

    auto value = rstd::env::var_os("RSTD_TEST_VAR_OS");
    ASSERT_TRUE(value.is_some());
    auto bytes = value->as_os_str().as_encoded_bytes();
    ASSERT_EQ(bytes.len(), 2u);
    EXPECT_EQ(bytes[0], static_cast<rstd::u8>('v'));
    EXPECT_EQ(bytes[1], static_cast<rstd::u8>(0xff));
    EXPECT_TRUE(value->as_os_str().to_str().is_none());

    rstd::env::remove_var("RSTD_TEST_VAR_OS");
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

TEST(Env, ArgsOsPreservesBytes) {
    const char  invalid[] = { 'v', static_cast<char>(0xff), '\0' };
    const char* argv[]    = { "prog", invalid };
    rstd::env::args_init(2, argv);

    auto args = rstd::env::args_os();
    EXPECT_EQ(rstd::as<rstd::iter::ExactSizeIterator>(args).len(), 2u);

    auto last = args.next_back();
    ASSERT_TRUE(last.is_some());
    auto bytes = last->as_os_str().as_encoded_bytes();
    ASSERT_EQ(bytes.len(), 2u);
    EXPECT_EQ(bytes[0], static_cast<rstd::u8>('v'));
    EXPECT_EQ(bytes[1], static_cast<rstd::u8>(0xff));
    EXPECT_TRUE(last->as_os_str().to_str().is_none());
}

TEST(Env, ArgsRejectInvalidUnicodeDuringIteration) {
    EXPECT_DEATH(iterate_invalid_unicode_arg(), "not valid Unicode");
}

TEST(Process, Id) {
    auto pid = rstd::process::id();
    EXPECT_GT(pid, 0u);
}

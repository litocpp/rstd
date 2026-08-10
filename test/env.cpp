#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

static void iterate_invalid_unicode_arg() {
    const char  invalid[] = { 'v', static_cast<char>(0xff), '\0' };
    const char* argv[]    = { "prog", invalid };
    rstd::env::args_init(2, argv);

    auto args = rstd::env::args();
    (void)args.next();
    (void)args.next();
}

TEST(Env, VarNotFound) {
    auto val = rstd::env::var("RSTD_TEST_NONEXISTENT_VAR_12345"_str);
    EXPECT_TRUE(val.is_none());
}

TEST(Env, TempDir) {
    auto path = rstd::env::temp_dir();
    EXPECT_FALSE(path.is_empty());
}

TEST(Env, SplitAndJoinPaths) {
#if RSTD_OS_WINDOWS
    auto value = rstd::ffi::OsString::from(R"(C:\bin;"D:\tool;bin";;relative)"_str);
#else
    auto value = rstd::ffi::OsString::from("/bin:/usr/bin::relative"_str);
#endif
    auto paths =
        rstd::env::split_paths(value.as_os_str()).collect<rstd::vec::Vec<rstd::path::PathBuf>>();
    ASSERT_EQ(paths.len(), rstd::usize(4));
    EXPECT_TRUE(paths[rstd::usize(2)].is_empty());

    auto joined = rstd::env::join_paths(paths.as_slice());
    ASSERT_TRUE(joined.is_ok());
    auto round_trip =
        rstd::env::split_paths(joined->as_os_str()).collect<rstd::vec::Vec<rstd::path::PathBuf>>();
    ASSERT_EQ(round_trip.len(), paths.len());
    for (auto index = rstd::usize(); index < paths.len(); ++index) {
        EXPECT_EQ(round_trip[index].as_path().as_os_str().as_encoded_bytes(),
                  paths[index].as_path().as_os_str().as_encoded_bytes());
    }
}

TEST(Env, JoinPathsRejectsInvalidSegment) {
    auto paths = rstd::vec::Vec<rstd::path::PathBuf>::make();
#if RSTD_OS_WINDOWS
    paths.push(rstd::path::PathBuf::from("bad\"path"_str));
#else
    paths.push(rstd::path::PathBuf::from("bad:path"_str));
#endif
    EXPECT_TRUE(rstd::env::join_paths(paths.as_slice()).is_err());
}

TEST(Env, SetAndGet) {
    rstd::env::set_var("RSTD_TEST_VAR"_str, "hello"_str);
    auto val = rstd::env::var("RSTD_TEST_VAR"_str);
    ASSERT_TRUE(val.is_some());
    EXPECT_EQ("hello"_str, val.unwrap());
}

TEST(Env, VarOsPreservesInvalidUnicode) {
    auto invalid = rstd::ref<rstd::ffi::OsStr>::from_encoded_bytes_unchecked("v\xff"_bytes);
    rstd::env::set_var("RSTD_TEST_VAR_OS"_str, invalid);

    auto value = rstd::env::var_os("RSTD_TEST_VAR_OS"_str);
    ASSERT_TRUE(value.is_some());
    auto os_str = value->as_os_str();
    auto bytes  = os_str.as_encoded_bytes();
    ASSERT_EQ(bytes.len(), rstd::usize(2));
    EXPECT_EQ(bytes[rstd::usize()], rstd::u8('v'));
    EXPECT_EQ(bytes[rstd::usize(1)], rstd::u8(0xff));
    EXPECT_TRUE(value->as_os_str().to_str().is_none());

    rstd::env::remove_var("RSTD_TEST_VAR_OS"_str);
}

TEST(Env, RemoveVar) {
    rstd::env::set_var("RSTD_TEST_REMOVE"_str, "value"_str);
    ASSERT_TRUE(rstd::env::var("RSTD_TEST_REMOVE"_str).is_some());

    rstd::env::remove_var("RSTD_TEST_REMOVE"_str);
    EXPECT_TRUE(rstd::env::var("RSTD_TEST_REMOVE"_str).is_none());
}

TEST(Env, OverwriteVar) {
    rstd::env::set_var("RSTD_TEST_OVERWRITE"_str, "first"_str);
    rstd::env::set_var("RSTD_TEST_OVERWRITE"_str, "second"_str);
    auto val = rstd::env::var("RSTD_TEST_OVERWRITE"_str);
    ASSERT_TRUE(val.is_some());
    EXPECT_EQ("second"_str, val.unwrap());
}

TEST(Env, Args) {
    // glibc .init_array captured the real process argv; there is always argv[0].
    auto n = rstd::env::args().count();
    EXPECT_GE(n, rstd::usize(1));

    auto first = rstd::env::args().next();
    ASSERT_TRUE(first.is_some());
    EXPECT_GT(first.unwrap().len(), rstd::usize()); // program path is non-empty
}

TEST(Env, ArgsManualInit) {
    const char* argv[] = { "prog", "--flag", "value" };
    rstd::env::args_init(3, argv);

    auto collected = rstd::env::args().collect<rstd::vec::Vec<rstd::string::String>>();
    ASSERT_EQ(collected.len(), rstd::usize(3));
    EXPECT_EQ("prog"_str, collected[rstd::usize()]);
    EXPECT_EQ("--flag"_str, collected[rstd::usize(1)]);
    EXPECT_EQ("value"_str, collected[rstd::usize(2)]);

    // args() is an ExactSize + DoubleEnded iterator
    auto it = rstd::env::args();
    EXPECT_EQ(rstd::as<rstd::iter::ExactSizeIterator>(it).len(), rstd::usize(3));
    auto last = rstd::env::args().next_back();
    ASSERT_TRUE(last.is_some());
    EXPECT_EQ("value"_str, last.unwrap());
}

TEST(Env, ArgsOsPreservesBytes) {
    const char  invalid[] = { 'v', static_cast<char>(0xff), '\0' };
    const char* argv[]    = { "prog", invalid };
    rstd::env::args_init(2, argv);

    auto args = rstd::env::args_os();
    EXPECT_EQ(rstd::as<rstd::iter::ExactSizeIterator>(args).len(), rstd::usize(2));

    auto last = args.next_back();
    ASSERT_TRUE(last.is_some());
    auto os_str = last->as_os_str();
    auto bytes  = os_str.as_encoded_bytes();
    ASSERT_EQ(bytes.len(), rstd::usize(2));
    EXPECT_EQ(bytes[rstd::usize()], rstd::u8('v'));
    EXPECT_EQ(bytes[rstd::usize(1)], rstd::u8(0xff));
    EXPECT_TRUE(last->as_os_str().to_str().is_none());
}

TEST(Env, ArgsRejectInvalidUnicodeDuringIteration) {
    EXPECT_DEATH(iterate_invalid_unicode_arg(), "not valid Unicode");
}

TEST(Process, Id) {
    auto pid = rstd::process::id();
    EXPECT_GT(pid, rstd::u32());
}

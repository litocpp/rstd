#include <gtest/gtest.h>
#include <vector>

import rstd;

using namespace rstd::prelude;

TEST(Str, IsEmpty) {
    EXPECT_TRUE(rstd::str_::is_empty(""));
    EXPECT_FALSE(rstd::str_::is_empty("hi"));
}

TEST(Str, IsAscii) {
    EXPECT_TRUE(rstd::str_::is_ascii("hello"));
    EXPECT_FALSE(rstd::str_::is_ascii("héllo"));
}

TEST(Str, Contains) {
    EXPECT_TRUE(rstd::str_::contains("hello world", "world"));
    EXPECT_TRUE(rstd::str_::contains("hello", ""));
    EXPECT_FALSE(rstd::str_::contains("hello", "xyz"));
}

TEST(Str, StartsWith) {
    EXPECT_TRUE(rstd::str_::starts_with("hello world", "hello"));
    EXPECT_FALSE(rstd::str_::starts_with("hello", "world"));
    EXPECT_TRUE(rstd::str_::starts_with("hello", ""));
}

TEST(Str, EndsWith) {
    EXPECT_TRUE(rstd::str_::ends_with("hello world", "world"));
    EXPECT_FALSE(rstd::str_::ends_with("hello", "world"));
}

TEST(Str, Find) {
    auto r = rstd::str_::find("hello world", "world");
    ASSERT_TRUE(r.is_some());
    EXPECT_EQ(r.unwrap(), 6u);

    EXPECT_TRUE(rstd::str_::find("hello", "xyz").is_none());

    auto z = rstd::str_::find("hello", "");
    ASSERT_TRUE(z.is_some());
    EXPECT_EQ(z.unwrap(), 0u);
}

TEST(Str, Trim) {
    EXPECT_EQ(rstd::str_::trim("  hello  "), rstd::ref<rstd::str>("hello"));
    EXPECT_EQ(rstd::str_::trim("\t\n hi \r\n"), rstd::ref<rstd::str>("hi"));
    EXPECT_EQ(rstd::str_::trim(""), rstd::ref<rstd::str>(""));
}

TEST(Str, SplitAt) {
    auto [a, b] = rstd::str_::split_at("hello", 2);
    EXPECT_EQ(a, rstd::ref<rstd::str>("he"));
    EXPECT_EQ(b, rstd::ref<rstd::str>("llo"));
}

TEST(Str, CharsAscii) {
    std::vector<char32_t> cps;
    auto it = rstd::str_::chars("ABC");
    for (auto c : it) cps.push_back(c);
    ASSERT_EQ(cps.size(), 3u);
    EXPECT_EQ(cps[0], U'A');
    EXPECT_EQ(cps[1], U'B');
    EXPECT_EQ(cps[2], U'C');
}

TEST(Str, CharsMultibyte) {
    // "中文" = 2 code points, 6 bytes
    std::vector<char32_t> cps;
    for (auto c : rstd::str_::chars("\xe4\xb8\xad\xe6\x96\x87")) cps.push_back(c);
    ASSERT_EQ(cps.size(), 2u);
    EXPECT_EQ(cps[0], char32_t(0x4E2D)); // 中
    EXPECT_EQ(cps[1], char32_t(0x6587)); // 文
}

TEST(Str, CharsEmoji) {
    // "😀" = 1 code point, 4 bytes
    std::vector<char32_t> cps;
    for (auto c : rstd::str_::chars("\xf0\x9f\x98\x80")) cps.push_back(c);
    ASSERT_EQ(cps.size(), 1u);
    EXPECT_EQ(cps[0], char32_t(0x1F600));
}

TEST(Str, FromUtf8Valid) {
    rstd::u8 data[] = { 'h', 'i' };
    auto sl = rstd::slice<rstd::u8>::from_raw_parts(data, 2);
    auto r = rstd::str_::from_utf8(sl);
    ASSERT_TRUE(r.is_some());
    EXPECT_EQ(r.unwrap(), rstd::ref<rstd::str>("hi"));
}

TEST(Str, FromUtf8Invalid) {
    rstd::u8 data[] = { 0xFF, 0xFE };
    auto sl = rstd::slice<rstd::u8>::from_raw_parts(data, 2);
    EXPECT_TRUE(rstd::str_::from_utf8(sl).is_none());
}

TEST(String, MakeFromStr) {
    auto s = rstd::string::String::make("hello");
    EXPECT_EQ(s.len(), 5u);
    EXPECT_EQ("hello", s);
}

TEST(String, MakeFromRefStr) {
    rstd::ref<rstd::str> r("world");
    auto s = rstd::string::String::make(r);
    EXPECT_EQ(s.len(), 5u);
    EXPECT_EQ("world", s);
}

TEST(String, PushCodepoint) {
    auto s = rstd::string::String::make("hi");
    s.push(U'!');
    EXPECT_EQ("hi!", s);

    s.push(char32_t(0x4E2D)); // 中
    EXPECT_EQ(s.len(), 6u); // "hi!" (3) + "中" (3 bytes)
}

TEST(String, AsStr) {
    auto s = rstd::string::String::make("test");
    auto r = s.as_str();
    EXPECT_EQ(r, rstd::ref<rstd::str>("test"));
}

TEST(String, Truncate) {
    auto s = rstd::string::String::make("hello");
    s.truncate(3);
    EXPECT_EQ("hel", s);
    EXPECT_EQ(s.len(), 3u);
}

TEST(String, ClearAndIsEmpty) {
    auto s = rstd::string::String::make("hello");
    EXPECT_FALSE(s.is_empty());
    s.clear();
    EXPECT_TRUE(s.is_empty());
    EXPECT_EQ(s.len(), 0u);
}

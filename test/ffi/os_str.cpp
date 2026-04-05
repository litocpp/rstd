import rstd;
#include <gtest/gtest.h>

using rstd::ffi::OsStr;
using rstd::ffi::OsString;
using rstd::string::String;

TEST(OsStr, FromStr) {
    rstd::ref<OsStr> s("hello");
    EXPECT_EQ(s.len(), 5u);
    EXPECT_FALSE(s.is_empty());
}

TEST(OsStr, ToStrValid) {
    rstd::ref<OsStr> s("hello");
    auto r = s.to_str();
    ASSERT_TRUE(r.is_some());
    EXPECT_EQ(r.unwrap(), rstd::ref<rstd::str>("hello"));
}

TEST(OsStr, ToStrInvalid) {
    rstd::u8 bad[] = { 0xFF, 0xFE };
    rstd::ref<OsStr> s(bad, 2);
    EXPECT_TRUE(s.to_str().is_none());
}

TEST(OsStr, ToStringLossy) {
    // Valid UTF-8 passes through
    rstd::ref<OsStr> valid("hello");
    auto s1 = valid.to_string_lossy();
    EXPECT_EQ("hello", s1);

    // Invalid bytes become U+FFFD
    rstd::u8 mixed[] = { 'h', 0xFF, 'i' };
    rstd::ref<OsStr> invalid(mixed, 3);
    auto s2 = invalid.to_string_lossy();
    // "h" + U+FFFD (3 bytes) + "i" = 5 bytes
    EXPECT_EQ(s2.len(), 5u); // 'h'(1) + U+FFFD(3) + 'i'(1)
}

TEST(OsString, MakeEmpty) {
    auto s = OsString::make();
    EXPECT_TRUE(s.is_empty());
    EXPECT_EQ(s.len(), 0u);
}

TEST(OsString, FromString) {
    auto str = String::make("hello");
    auto os = OsString::from(rstd::move(str));
    EXPECT_EQ(os.len(), 5u);
    auto r = os.as_os_str().to_str();
    ASSERT_TRUE(r.is_some());
    EXPECT_EQ(r.unwrap(), rstd::ref<rstd::str>("hello"));
}

TEST(OsString, FromRefStr) {
    auto os = OsString::from(rstd::ref<rstd::str>("world"));
    EXPECT_EQ(os.len(), 5u);
}

TEST(OsString, FromRefOsStr) {
    rstd::ref<OsStr> r("test");
    auto os = OsString::from(r);
    EXPECT_EQ(os.len(), 4u);
}

TEST(OsString, IntoStringValid) {
    auto os = OsString::from(rstd::ref<rstd::str>("utf8"));
    auto res = os.into_string();
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ("utf8", res.unwrap());
}

TEST(OsString, IntoStringInvalid) {
    rstd::u8 bad[] = { 0xFF };
    rstd::ref<OsStr> r(bad, 1);
    auto os = OsString::from(r);
    auto res = os.into_string();
    EXPECT_TRUE(res.is_err());
}

TEST(OsString, Push) {
    auto os = OsString::from(rstd::ref<rstd::str>("he"));
    os.push(rstd::ref<OsStr>("llo"));
    EXPECT_EQ(os.len(), 5u);
    auto s = os.as_os_str().to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(s.unwrap(), rstd::ref<rstd::str>("hello"));
}

TEST(OsString, Clear) {
    auto os = OsString::from(rstd::ref<rstd::str>("data"));
    EXPECT_FALSE(os.is_empty());
    os.clear();
    EXPECT_TRUE(os.is_empty());
}

TEST(OsString, ImplicitConversion) {
    auto os = OsString::from(rstd::ref<rstd::str>("conv"));
    rstd::ref<OsStr> r = os; // implicit conversion
    EXPECT_EQ(r.len(), 4u);
}

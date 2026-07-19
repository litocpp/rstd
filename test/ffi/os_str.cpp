#include <gtest/gtest.h>
import rstd;

using rstd::ffi::OsStr;
using rstd::ffi::OsString;
using rstd::string::String;

TEST(OsStr, FromStr) {
    rstd::ref<OsStr> s("hello");
    EXPECT_EQ(s.len(), rstd::usize(5));
    EXPECT_FALSE(s.is_empty());
}

TEST(OsStr, ToStrValid) {
    rstd::ref<OsStr> s("hello");
    auto             r = s.to_str();
    ASSERT_TRUE(r.is_some());
    EXPECT_EQ(r.unwrap(), rstd::ref<rstd::str>("hello"));
}

TEST(OsStr, ToStrInvalid) {
    rstd::uint8_t    bad[] = { 0xFF, 0xFE };
    rstd::ref<OsStr> s(bad, rstd::usize(2));
    EXPECT_TRUE(s.to_str().is_none());
}

TEST(OsStr, ToStringLossy) {
    // Valid UTF-8 passes through
    rstd::ref<OsStr> valid("hello");
    auto             s1 = valid.to_string_lossy();
    EXPECT_EQ("hello", s1);

    // Invalid bytes become U+FFFD
    rstd::uint8_t    mixed[] = { 'h', 0xFF, 'i' };
    rstd::ref<OsStr> invalid(mixed, rstd::usize(3));
    auto             s2 = invalid.to_string_lossy();
    // "h" + U+FFFD (3 bytes) + "i" = 5 bytes
    EXPECT_EQ(s2.len(), rstd::usize(5)); // 'h'(1) + U+FFFD(3) + 'i'(1)
}

TEST(OsStr, PrefixAndSplitPreserveArbitraryBytes) {
    rstd::uint8_t    bytes[] = { '-', '-', 'n', 'a', 'm', 'e', '=', 0xFF };
    rstd::ref<OsStr> value(bytes, rstd::usize(8));

    EXPECT_TRUE(value.starts_with(rstd::ref<OsStr>("--")));
    auto stripped = value.strip_prefix(rstd::ref<OsStr>("--"));
    ASSERT_TRUE(stripped.is_some());
    auto split = stripped->split_once(rstd::u8('='));
    ASSERT_TRUE(split.is_some());
    EXPECT_EQ(split->template get<0>().to_str(), rstd::Some(rstd::ref<rstd::str>("name")));
    EXPECT_EQ(split->template get<1>().len(), rstd::usize(1));
    EXPECT_EQ(split->template get<1>().data()[0], 0xFF);
}

TEST(OsString, MakeEmpty) {
    auto s = OsString::make();
    EXPECT_TRUE(s.is_empty());
    EXPECT_EQ(s.len(), rstd::usize());
}

TEST(OsString, FromString) {
    auto str = String::make("hello");
    auto os  = OsString::from(rstd::move(str));
    EXPECT_EQ(os.len(), rstd::usize(5));
    auto r = os.as_os_str().to_str();
    ASSERT_TRUE(r.is_some());
    EXPECT_EQ(r.unwrap(), rstd::ref<rstd::str>("hello"));
}

TEST(OsString, FromRefStr) {
    auto os = OsString::from(rstd::ref<rstd::str>("world"));
    EXPECT_EQ(os.len(), rstd::usize(5));
}

TEST(OsString, FromRefOsStr) {
    rstd::ref<OsStr> r("test");
    auto             os = OsString::from(r);
    EXPECT_EQ(os.len(), rstd::usize(4));
}

TEST(OsString, IntoStringValid) {
    auto os  = OsString::from(rstd::ref<rstd::str>("utf8"));
    auto res = os.into_string();
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ("utf8", res.unwrap());
}

TEST(OsString, IntoStringInvalid) {
    rstd::uint8_t    bad[] = { 0xFF };
    rstd::ref<OsStr> r(bad, rstd::usize(1));
    auto             os  = OsString::from(r);
    auto             res = os.into_string();
    EXPECT_TRUE(res.is_err());
}

TEST(OsString, TryIntoStringUsesOwnedConversion) {
    auto valid = OsString::from(rstd::ref<rstd::str>("utf8"));
    EXPECT_EQ("utf8", rstd::try_into<String>(rstd::move(valid)).unwrap());

    rstd::uint8_t    bad[] = { 0xFF };
    rstd::ref<OsStr> bytes(bad, rstd::usize(1));
    auto             invalid = OsString::from(bytes);
    EXPECT_EQ(rstd::try_from<String>(rstd::move(invalid)).unwrap_err().len(), rstd::usize(1));
}

TEST(OsString, Push) {
    auto os = OsString::from(rstd::ref<rstd::str>("he"));
    os.push(rstd::ref<OsStr>("llo"));
    EXPECT_EQ(os.len(), rstd::usize(5));
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
    auto             os = OsString::from(rstd::ref<rstd::str>("conv"));
    rstd::ref<OsStr> r  = os; // implicit conversion
    EXPECT_EQ(r.len(), rstd::usize(4));
}

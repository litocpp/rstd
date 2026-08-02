#include <rstd/test/gtest.hpp>
import rstd;

using rstd::ffi::OsStr;
using rstd::ffi::OsString;
using rstd::string::String;
using namespace rstd::literals;

TEST(OsStr, FromStr) {
    rstd::ref<OsStr> s("hello"_str);
    EXPECT_EQ(s.len(), rstd::usize(5));
    EXPECT_FALSE(s.is_empty());
}

TEST(OsStr, ToStrValid) {
    rstd::ref<OsStr> s("hello"_str);
    auto             r = s.to_str();
    ASSERT_TRUE(r.is_some());
    EXPECT_EQ(r.unwrap(), "hello"_str);
}

TEST(OsStr, ToStrInvalid) {
    auto s = rstd::ref<OsStr>::from_encoded_bytes_unchecked("\xff\xfe"_bytes);
    EXPECT_TRUE(s.to_str().is_none());
}

TEST(OsStr, ToStringLossy) {
    // Valid UTF-8 passes through
    rstd::ref<OsStr> valid("hello"_str);
    auto             s1 = valid.to_string_lossy();
    EXPECT_EQ("hello"_str, s1);

    // Invalid bytes become U+FFFD
    auto invalid = rstd::ref<OsStr>::from_encoded_bytes_unchecked("h\xffi"_bytes);
    auto s2      = invalid.to_string_lossy();
    // "h" + U+FFFD (3 bytes) + "i" = 5 bytes
    EXPECT_EQ(s2.len(), rstd::usize(5)); // 'h'(1) + U+FFFD(3) + 'i'(1)
}

TEST(OsStr, PrefixAndSplitPreserveArbitraryBytes) {
    auto value = rstd::ref<OsStr>::from_encoded_bytes_unchecked("--name=\xff"_bytes);

    EXPECT_TRUE(value.starts_with(rstd::ref<OsStr>("--"_str)));
    auto stripped = value.strip_prefix(rstd::ref<OsStr>("--"_str));
    ASSERT_TRUE(stripped.is_some());
    auto split = stripped->split_once(rstd::u8('='));
    ASSERT_TRUE(split.is_some());
    EXPECT_EQ(split->template get<0>().to_str(), rstd::Some("name"_str));
    EXPECT_EQ(split->template get<1>().len(), rstd::usize(1));
    EXPECT_EQ(split->template get<1>().as_encoded_bytes()[rstd::usize()], rstd::u8(0xff));
}

TEST(OsString, MakeEmpty) {
    auto s = OsString::make();
    EXPECT_TRUE(s.is_empty());
    EXPECT_EQ(s.len(), rstd::usize());
}

TEST(OsString, FromString) {
    auto str = String::make("hello"_str);
    auto os  = OsString::from(rstd::move(str));
    EXPECT_EQ(os.len(), rstd::usize(5));
    auto r = os.as_os_str().to_str();
    ASSERT_TRUE(r.is_some());
    EXPECT_EQ(r.unwrap(), "hello"_str);
}

TEST(OsString, FromRefStr) {
    auto os = OsString::from("world"_str);
    EXPECT_EQ(os.len(), rstd::usize(5));
}

TEST(OsString, FromRefOsStr) {
    rstd::ref<OsStr> r("test"_str);
    auto             os = OsString::from(r);
    EXPECT_EQ(os.len(), rstd::usize(4));
}

TEST(OsString, IntoStringValid) {
    auto os  = OsString::from("utf8"_str);
    auto res = os.into_string();
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ("utf8"_str, res.unwrap());
}

TEST(OsString, IntoStringInvalid) {
    auto r   = rstd::ref<OsStr>::from_encoded_bytes_unchecked("\xff"_bytes);
    auto os  = OsString::from(r);
    auto res = os.into_string();
    EXPECT_TRUE(res.is_err());
}

TEST(OsString, TryIntoStringUsesOwnedConversion) {
    auto valid = OsString::from("utf8"_str);
    EXPECT_EQ("utf8"_str, rstd::try_into<String>(rstd::move(valid)).unwrap());

    auto bytes   = rstd::ref<OsStr>::from_encoded_bytes_unchecked("\xff"_bytes);
    auto invalid = OsString::from(bytes);
    EXPECT_EQ(rstd::try_from<String>(rstd::move(invalid)).unwrap_err().len(), rstd::usize(1));
}

TEST(OsString, Push) {
    auto os = OsString::from("he"_str);
    os.push(rstd::ref<OsStr>("llo"_str));
    EXPECT_EQ(os.len(), rstd::usize(5));
    auto s = os.as_os_str().to_str();
    ASSERT_TRUE(s.is_some());
    EXPECT_EQ(s.unwrap(), "hello"_str);
}

TEST(OsString, Clear) {
    auto os = OsString::from("data"_str);
    EXPECT_FALSE(os.is_empty());
    os.clear();
    EXPECT_TRUE(os.is_empty());
}

TEST(OsString, ImplicitConversion) {
    auto             os = OsString::from("conv"_str);
    rstd::ref<OsStr> r  = os; // implicit conversion
    EXPECT_EQ(r.len(), rstd::usize(4));
}

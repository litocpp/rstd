#include <rstd/test/gtest.hpp>
#include <rstd/macro.hpp>
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

#if RSTD_OS_UNIX
TEST(OsStr, ToStrInvalid) {
    auto s = rstd::ref<OsStr>::from_encoded_bytes_unchecked("\xff\xfe"_bytes);
    EXPECT_TRUE(s.to_str().is_none());
}
#endif

#if RSTD_OS_UNIX
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
#endif

#if RSTD_OS_UNIX
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
#endif

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

#if RSTD_OS_UNIX
TEST(OsString, ClonePreservesArbitraryBytesAndOwnership) {
    auto bytes = rstd::ref<OsStr>::from_encoded_bytes_unchecked("a\xff"_bytes);
    auto value = OsString::from(bytes);
    auto copy  = value.clone();

    value.clear();
    EXPECT_TRUE(value.is_empty());
    EXPECT_EQ(copy.as_os_str().as_encoded_bytes(), bytes.as_encoded_bytes());

    auto assigned = OsString::from("old"_str);
    assigned.clone_from(copy);
    EXPECT_EQ(assigned.as_os_str().as_encoded_bytes(), bytes.as_encoded_bytes());
}
#endif

TEST(OsString, IntoStringValid) {
    auto os  = OsString::from("utf8"_str);
    auto res = os.into_string();
    ASSERT_TRUE(res.is_ok());
    EXPECT_EQ("utf8"_str, res.unwrap());
}

#if RSTD_OS_UNIX
TEST(OsStr, LossyReplacementGroupsIncompleteSequences) {
    auto value = rstd::os::unix::ffi::OsStrExt::from_bytes("a\xe2\x82!\xf0\x90\x80"_bytes);
    EXPECT_EQ(value.to_string_lossy(), "a�!�"_str);
    EXPECT_EQ(rstd::format("{}", value.display()), "a�!�"_str);
    auto path = rstd::path::PathBuf::from(value.to_os_string());
    EXPECT_EQ(rstd::format("{}", path.as_path()), "a�!�"_str);
}

TEST(OsString, IntoStringInvalid) {
    auto r   = rstd::ref<OsStr>::from_encoded_bytes_unchecked("\xff"_bytes);
    auto os  = OsString::from(r);
    auto res = os.into_string();
    EXPECT_TRUE(res.is_err());
}
#endif

#if RSTD_OS_UNIX
TEST(OsString, TryIntoStringUsesOwnedConversion) {
    auto valid = OsString::from("utf8"_str);
    EXPECT_EQ("utf8"_str, rstd::try_into<String>(rstd::move(valid)).unwrap());

    auto bytes   = rstd::ref<OsStr>::from_encoded_bytes_unchecked("\xff"_bytes);
    auto invalid = OsString::from(bytes);
    EXPECT_EQ(rstd::try_from<String>(rstd::move(invalid)).unwrap_err().len(), rstd::usize(1));
}
#endif

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

TEST(OsString, CapacityAndSelfAppend) {
    auto value = OsString::with_capacity(rstd::usize(2));
    value.push("ab"_str);
    value.push(value.as_os_str());
    EXPECT_EQ(value.as_os_str().to_str().unwrap(), "abab"_str);
    value.reserve_exact(rstd::usize(40));
    EXPECT_GE(value.capacity(), rstd::usize(44));
    value.shrink_to(rstd::usize(12));
    EXPECT_GE(value.capacity(), rstd::usize(12));
    value.shrink_to_fit();
    EXPECT_EQ(value.capacity(), value.len());
    EXPECT_TRUE(value.try_reserve(rstd::usize(8)).is_ok());
    EXPECT_TRUE(value.try_reserve_exact(rstd::usize::MAX).is_err());
    EXPECT_EQ(value.as_os_str().to_str().unwrap(), "abab"_str);
}

TEST(OsString, AsciiOperationsPreserveUnicode) {
    auto value = OsString::from("HéLLo"_str);
    EXPECT_FALSE(value.is_ascii());
    auto lower = value.as_os_str().to_ascii_lowercase();
    EXPECT_EQ(lower.as_os_str().to_str().unwrap(), "héllo"_str);
    value.make_ascii_uppercase();
    EXPECT_EQ(value.as_os_str().to_str().unwrap(), "HéLLO"_str);
    EXPECT_TRUE(value.eq_ignore_ascii_case(lower.as_os_str()));
    EXPECT_FALSE(value.eq_ignore_ascii_case("HELLO"_str));
    value.truncate(rstd::usize(3));
    EXPECT_EQ(value.as_os_str().to_str().unwrap(), "Hé"_str);
    EXPECT_EQ(rstd::format("{}", value.as_os_str().display()), "Hé"_str);
}

TEST(OsString, EncodedOwnershipAndHash) {
    auto value = OsString::from("路径"_str);
    auto copy  = value.as_os_str().to_os_string();
    EXPECT_TRUE(value == copy);
    rstd::hash::DefaultHasher left;
    rstd::hash::DefaultHasher right;
    rstd::hash::hash_into(value, left);
    rstd::hash::hash_into(copy.as_os_str(), right);
    EXPECT_EQ(left.finish(), right.finish());
    auto bytes = rstd::move(value).into_encoded_bytes();
    EXPECT_EQ(bytes.as_slice(), "路径"_str.as_bytes());
}

#if RSTD_OS_WINDOWS
TEST(OsString, WindowsWideRoundTripAndNativeConcatenation) {
    using rstd::os::windows::ffi::OsStringExt;
    using rstd::os::windows::ffi::OsStrExt;
    const rstd::u16 units[] = { rstd::u16(0xd800), rstd::u16('a'), rstd::u16(0xdc00) };
    auto            value =
        OsStringExt::from_wide(rstd::slice<rstd::u16>::from_raw_parts(units, rstd::usize(3)));
    EXPECT_TRUE(value.as_os_str().to_str().is_none());
    auto roundtrip = OsStrExt::encode_wide(value.as_os_str()).collect<rstd::vec::Vec<rstd::u16>>();
    EXPECT_EQ(roundtrip.as_slice(), rstd::slice<rstd::u16>::from_raw_parts(units, rstd::usize(3)));
    EXPECT_EQ(value.as_os_str().to_string_lossy(), "�a�"_str);
    auto lead =
        OsStringExt::from_wide(rstd::slice<rstd::u16>::from_raw_parts(units, rstd::usize(1)));
    auto trail =
        OsStringExt::from_wide(rstd::slice<rstd::u16>::from_raw_parts(units + 2, rstd::usize(1)));
    lead.push(trail.as_os_str());
    EXPECT_EQ(lead.as_os_str().as_encoded_bytes(), "\xf0\x90\x80\x80"_bytes);
}
#endif

#include <gtest/gtest.h>
#include <vector>

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

template<typename T>
concept HasMutableRawPointer = requires(T value) { value.as_mut_ptr(); };

template<typename T>
concept HasRawPointer = requires(T value) { value.as_raw_ptr(); };

consteval bool mutable_str_ascii_lowercase_is_constexpr() {
    rstd::byte storage[] = { rstd::byte { '@' }, rstd::byte { 'A' }, rstd::byte { 'Z' },
                             rstd::byte { '[' }, rstd::byte { 'a' }, rstd::byte { 'z' } };
    auto       bytes     = rstd::mut_ref<rstd::u8[]>::from_raw_parts(storage, usize(6));
    auto       value     = rstd::from_utf8_unchecked_mut(bytes);
    value.make_ascii_lowercase();
    return value[usize()] == u8('@') && value[usize(1)] == u8('a') && value[usize(2)] == u8('z') &&
           value[usize(3)] == u8('[') && value[usize(4)] == u8('a') && value[usize(5)] == u8('z');
}

inline constexpr auto BYTE_LITERAL = "a\0\xff"_b;
inline constexpr auto BYTE_VIEW    = "a\0\xff"_bytes;
inline constexpr auto UTF8_LITERAL = "é中😀"_str;

static_assert(BYTE_LITERAL.size() == 3);
static_assert(BYTE_LITERAL[0] == rstd::byte { 'a' });
static_assert(BYTE_LITERAL[1] == rstd::byte {});
static_assert(BYTE_LITERAL[2] == rstd::byte { 0xff });
static_assert(BYTE_VIEW.len() == usize(3));
static_assert(BYTE_VIEW[usize(2)] == rstd::u8(0xff));
static_assert(UTF8_LITERAL.size() == usize(9));
static_assert(rstd::str_::is_char_boundary(UTF8_LITERAL, usize(1)) == false);
static_assert(rstd::mtp::same_as<decltype(UTF8_LITERAL.begin()), rstd::ptr<rstd::u8>>);
static_assert(mutable_str_ascii_lowercase_is_constexpr());
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::mut_ref<rstd::str>>().data()),
                                 rstd::byte const*>);
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::mut_ref<rstd::str>>().begin()),
                                 rstd::ptr<rstd::u8>>);
static_assert(! HasMutableRawPointer<rstd::mut_ref<rstd::str>>);
static_assert(! HasRawPointer<rstd::mut_ref<rstd::str>>);
static_assert(! HasRawPointer<rstd::ref<rstd::str>>);

TEST(Str, IsEmpty) {
    EXPECT_TRUE(rstd::str_::is_empty(""_str));
    EXPECT_FALSE(rstd::str_::is_empty("hi"_str));
}

TEST(Str, RangeForYieldsUtf8CodeUnits) {
    auto total = rstd::u32();
    for (auto value : "A右"_str) total += rstd::u32(value.to_primitive());
    EXPECT_EQ(total, rstd::u32('A' + 0xe5 + 0x8f + 0xb3));
}

TEST(Str, IsAscii) {
    EXPECT_TRUE(rstd::str_::is_ascii("hello"_str));
    EXPECT_FALSE(rstd::str_::is_ascii("héllo"_str));
}

TEST(Str, MutableViewMakesOnlyAsciiLowercase) {
    rstd::byte storage[] = { rstd::byte { 'G' },  rstd::byte { 'R' },  rstd::byte { 0xc3 },
                             rstd::byte { 0x9c }, rstd::byte { 0xc3 }, rstd::byte { 0x9f },
                             rstd::byte { 'E' },  rstd::byte {},       rstd::byte { 'Z' } };
    auto       bytes     = rstd::mut_ref<rstd::u8[]>::from_raw_parts(storage, usize(9));
    auto       value     = rstd::from_utf8_unchecked_mut(bytes);

    value->make_ascii_lowercase();

    EXPECT_EQ(value.as_ref(), "grÜße\0z"_str);
    EXPECT_EQ(value.size(), usize(9));
    EXPECT_EQ(value.data(), storage);
    EXPECT_EQ(storage[2], rstd::byte { 0xc3 });
    EXPECT_EQ(storage[3], rstd::byte { 0x9c });
    EXPECT_EQ(storage[4], rstd::byte { 0xc3 });
    EXPECT_EQ(storage[5], rstd::byte { 0x9f });
}

TEST(Str, Contains) {
    EXPECT_TRUE(rstd::str_::contains("hello world"_str, "world"_str));
    EXPECT_TRUE(rstd::str_::contains("hello"_str, ""_str));
    EXPECT_FALSE(rstd::str_::contains("hello"_str, "xyz"_str));
}

TEST(Str, StartsWith) {
    EXPECT_TRUE(rstd::str_::starts_with("hello world"_str, "hello"_str));
    EXPECT_FALSE(rstd::str_::starts_with("hello"_str, "world"_str));
    EXPECT_TRUE(rstd::str_::starts_with("hello"_str, ""_str));
}

TEST(Str, EndsWith) {
    EXPECT_TRUE(rstd::str_::ends_with("hello world"_str, "world"_str));
    EXPECT_FALSE(rstd::str_::ends_with("hello"_str, "world"_str));
}

TEST(Str, Find) {
    auto r = rstd::str_::find("hello world"_str, "world"_str);
    ASSERT_TRUE(r.is_some());
    EXPECT_EQ(r.unwrap(), usize(6));

    EXPECT_TRUE(rstd::str_::find("hello"_str, "xyz"_str).is_none());

    auto z = rstd::str_::find("hello"_str, ""_str);
    ASSERT_TRUE(z.is_some());
    EXPECT_EQ(z.unwrap(), usize());
}

TEST(Str, CheckedRangesAndAffixRemovalUseByteOffsets) {
    auto value = "a右bc右"_str;

    EXPECT_EQ(*rstd::str_::get(value, usize(1), usize(4)), "右"_str);
    EXPECT_TRUE(rstd::str_::get(value, usize(2), usize(4)).is_none());
    EXPECT_EQ(*rstd::str_::strip_prefix(value, "a右"_str), "bc右"_str);
    EXPECT_EQ(*rstd::str_::strip_suffix(value, "右"_str), "a右bc"_str);
    EXPECT_TRUE(rstd::str_::strip_prefix(value, "右"_str).is_none());
}

TEST(Str, SplitOnceAndReverseFindPreserveEmptySides) {
    auto value = "left::middle::right"_str;

    auto first = rstd::str_::split_once(value, "::"_str);
    ASSERT_TRUE(first.is_some());
    EXPECT_EQ(first->template get<0>(), "left"_str);
    EXPECT_EQ(first->template get<1>(), "middle::right"_str);

    auto last = rstd::str_::rsplit_once(value, "::"_str);
    ASSERT_TRUE(last.is_some());
    EXPECT_EQ(last->template get<0>(), "left::middle"_str);
    EXPECT_EQ(last->template get<1>(), "right"_str);
    EXPECT_EQ(*rstd::str_::rfind(value, ""_str), value.size());
}

TEST(Str, Trim) {
    EXPECT_EQ(rstd::str_::trim("  hello  "_str), "hello"_str);
    EXPECT_EQ(rstd::str_::trim("\t\n hi \r\n"_str), "hi"_str);
    EXPECT_EQ(rstd::str_::trim(""_str), ""_str);
}

TEST(Str, SplitAt) {
    auto [a, b] = rstd::str_::split_at("hello"_str, usize(2));
    EXPECT_EQ(a, "he"_str);
    EXPECT_EQ(b, "llo"_str);
}

TEST(Str, CharsAscii) {
    std::vector<char32_t> cps;
    auto                  it = rstd::str_::chars("ABC"_str);
    for (auto c : it) cps.push_back(c);
    ASSERT_EQ(cps.size(), 3u);
    EXPECT_EQ(cps[0], U'A');
    EXPECT_EQ(cps[1], U'B');
    EXPECT_EQ(cps[2], U'C');
}

TEST(Str, CharsMultibyte) {
    // "中文" = 2 code points, 6 bytes
    std::vector<char32_t> cps;
    for (auto c : rstd::str_::chars("\xe4\xb8\xad\xe6\x96\x87"_str)) cps.push_back(c);
    ASSERT_EQ(cps.size(), 2u);
    EXPECT_EQ(cps[0], char32_t(0x4E2D)); // 中
    EXPECT_EQ(cps[1], char32_t(0x6587)); // 文
}

TEST(Str, CharsEmoji) {
    // "😀" = 1 code point, 4 bytes
    std::vector<char32_t> cps;
    for (auto c : rstd::str_::chars("\xf0\x9f\x98\x80"_str)) cps.push_back(c);
    ASSERT_EQ(cps.size(), 1u);
    EXPECT_EQ(cps[0], char32_t(0x1F600));
}

TEST(Str, FromUtf8Valid) {
    auto r = rstd::str_::from_utf8("hi"_bytes);
    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.unwrap(), "hi"_str);
}

TEST(Str, FromUtf8Invalid) {
    auto result = rstd::str_::from_utf8("\xff\xfe"_bytes);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().valid_up_to(), usize());
}

TEST(Str, CharsExposesUnconsumedString) {
    auto chars = rstd::str_::chars("é中x"_str);
    EXPECT_EQ(chars.as_str(), "é中x"_str);
    EXPECT_EQ(chars.next_unchecked(), U'é');
    EXPECT_EQ(chars.as_str(), "中x"_str);
    EXPECT_EQ(chars.next_unchecked(), U'中');
    EXPECT_EQ(chars.as_str(), "x"_str);
}

TEST(String, MakeFromStr) {
    auto s = rstd::string::String::make("hello"_str);
    EXPECT_EQ(s.len(), usize(5));
    EXPECT_EQ("hello"_str, s);
}

TEST(String, MakeFromRefStr) {
    auto r = "world"_str;
    auto s = rstd::string::String::make(r);
    EXPECT_EQ(s.len(), usize(5));
    EXPECT_EQ("world"_str, s);
}

TEST(String, PushCodepoint) {
    auto s = rstd::string::String::make("hi"_str);
    s.push(U'!');
    EXPECT_EQ("hi!"_str, s);

    s.push(char32_t(0x4E2D));     // 中
    EXPECT_EQ(s.len(), usize(6)); // "hi!" (3) + "中" (3 bytes)
}

TEST(String, AsStr) {
    auto s = rstd::string::String::make("test"_str);
    auto r = s.as_str();
    EXPECT_EQ(r, "test"_str);
}

TEST(String, Truncate) {
    auto s = rstd::string::String::make("hello"_str);
    s.truncate(usize(3));
    EXPECT_EQ("hel"_str, s);
    EXPECT_EQ(s.len(), usize(3));
}

TEST(String, ClearAndIsEmpty) {
    auto s = rstd::string::String::make("hello"_str);
    EXPECT_FALSE(s.is_empty());
    s.clear();
    EXPECT_TRUE(s.is_empty());
    EXPECT_EQ(s.len(), usize());
}

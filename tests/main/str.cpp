#include <rstd/test/gtest.hpp>
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
    auto       value     = rstd::str_::from_utf8_unchecked_mut(bytes);
    value.make_ascii_lowercase();
    return value[usize()] == u8('@') && value[usize(1)] == u8('a') && value[usize(2)] == u8('z') &&
           value[usize(3)] == u8('[') && value[usize(4)] == u8('a') && value[usize(5)] == u8('z');
}

consteval bool str_view_methods_are_constexpr() {
    auto value = " a右b "_str;
    auto range = value.get(usize(2), usize(5));
    auto found = value.find("右"_str);
    return value.contains("右"_str) && value.starts_with(" "_str) && value.ends_with(" "_str) &&
           found.is_some() && *found == usize(2) && range.is_some() && *range == "右"_str &&
           value.trim_ascii() == "a右b"_str;
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
static_assert(UTF8_LITERAL.is_char_boundary(usize(1)) == false);
static_assert(rstd::mtp::same_as<decltype(UTF8_LITERAL.begin()), rstd::ptr<rstd::u8>>);
static_assert(mutable_str_ascii_lowercase_is_constexpr());
static_assert(str_view_methods_are_constexpr());
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::mut_ref<rstd::str>>().data()),
                                 rstd::byte const*>);
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::mut_ref<rstd::str>>().begin()),
                                 rstd::ptr<rstd::u8>>);
static_assert(! HasMutableRawPointer<rstd::mut_ref<rstd::str>>);
static_assert(! HasRawPointer<rstd::mut_ref<rstd::str>>);
static_assert(! HasRawPointer<rstd::ref<rstd::str>>);

TEST(Str, IsEmpty) {
    EXPECT_TRUE(""_str.is_empty());
    EXPECT_FALSE("hi"_str.is_empty());
}

TEST(Str, RangeForYieldsUtf8CodeUnits) {
    auto total = rstd::u32();
    for (auto value : "A右"_str) total += rstd::u32(value.to_primitive());
    EXPECT_EQ(total, rstd::u32('A' + 0xe5 + 0x8f + 0xb3));
}

TEST(Str, EmptyViewMethodsAvoidStorageAccess) {
    auto value = rstd::ref<rstd::str>();

    EXPECT_TRUE(value.is_empty());
    EXPECT_TRUE(value.is_ascii());
    EXPECT_TRUE(value.is_char_boundary(usize()));
    EXPECT_TRUE(value.contains(""_str));
    EXPECT_TRUE(value.starts_with(""_str));
    EXPECT_TRUE(value.ends_with(""_str));
    EXPECT_EQ(value.as_bytes().len(), usize());
    EXPECT_EQ(value.trim_ascii(), ""_str);

    auto [left, right] = value.split_at(usize());
    EXPECT_TRUE(left.is_empty());
    EXPECT_TRUE(right.is_empty());
}

TEST(Str, IsAscii) {
    EXPECT_TRUE("hello"_str.is_ascii());
    EXPECT_FALSE("héllo"_str.is_ascii());
}

TEST(Str, MutableViewMakesOnlyAsciiLowercase) {
    rstd::byte storage[] = { rstd::byte { 'G' },  rstd::byte { 'R' },  rstd::byte { 0xc3 },
                             rstd::byte { 0x9c }, rstd::byte { 0xc3 }, rstd::byte { 0x9f },
                             rstd::byte { 'E' },  rstd::byte {},       rstd::byte { 'Z' } };
    auto       bytes     = rstd::mut_ref<rstd::u8[]>::from_raw_parts(storage, usize(9));
    auto       value     = rstd::str_::from_utf8_unchecked_mut(bytes);

    value->make_ascii_lowercase();

    EXPECT_EQ(value.as_ref(), "grÜße\0z"_str);
    EXPECT_EQ(value.size(), usize(9));
    EXPECT_EQ(value.data(), storage);
    EXPECT_EQ(storage[2], rstd::byte { 0xc3 });
    EXPECT_EQ(storage[3], rstd::byte { 0x9c });
    EXPECT_EQ(storage[4], rstd::byte { 0xc3 });
    EXPECT_EQ(storage[5], rstd::byte { 0x9f });
}

TEST(Str, MutableViewDelegatesSharedMethods) {
    rstd::byte storage[] = { rstd::byte { ' ' },  rstd::byte { 'A' },  rstd::byte { 0xe5 },
                             rstd::byte { 0x8f }, rstd::byte { 0xb3 }, rstd::byte { ' ' } };
    auto       bytes     = rstd::mut_ref<rstd::u8[]>::from_raw_parts(storage, usize(6));
    auto       value     = rstd::str_::from_utf8_unchecked_mut(bytes);

    EXPECT_TRUE(value.contains("A右"_str));
    EXPECT_EQ(*value.find("右"_str), usize(2));
    EXPECT_EQ(value.trim_ascii(), "A右"_str);
    EXPECT_EQ(*value.get(usize(1), usize(5)), "A右"_str);
    EXPECT_EQ(value.as_bytes()[usize(2)], u8(0xe5));
}

TEST(Str, Contains) {
    EXPECT_TRUE("hello world"_str.contains("world"_str));
    EXPECT_TRUE("hello"_str.contains(""_str));
    EXPECT_TRUE("a\0b"_str.contains("\0b"_str));
    EXPECT_TRUE("same"_str.contains("same"_str));
    EXPECT_FALSE("hello"_str.contains("xyz"_str));
    EXPECT_FALSE("hi"_str.contains("longer"_str));
}

TEST(Str, StartsWith) {
    EXPECT_TRUE("hello world"_str.starts_with("hello"_str));
    EXPECT_FALSE("hello"_str.starts_with("world"_str));
    EXPECT_TRUE("hello"_str.starts_with(""_str));
}

TEST(Str, EndsWith) {
    EXPECT_TRUE("hello world"_str.ends_with("world"_str));
    EXPECT_FALSE("hello"_str.ends_with("world"_str));
    EXPECT_TRUE(""_str.ends_with(""_str));
}

TEST(Str, Find) {
    auto r = "hello world"_str.find("world"_str);
    ASSERT_TRUE(r.is_some());
    EXPECT_EQ(r.unwrap(), usize(6));

    EXPECT_TRUE("hello"_str.find("xyz"_str).is_none());

    auto z = "hello"_str.find(""_str);
    ASSERT_TRUE(z.is_some());
    EXPECT_EQ(z.unwrap(), usize());
}

TEST(Str, CheckedRangesAndAffixRemovalUseByteOffsets) {
    auto value = "a右bc右"_str;

    EXPECT_EQ(*value.get(usize(1), usize(4)), "右"_str);
    EXPECT_TRUE(value.get(usize(2), usize(4)).is_none());
    EXPECT_TRUE(value.get(usize(4), usize(2)).is_none());
    EXPECT_TRUE(value.get(usize(), usize(99)).is_none());
    EXPECT_EQ(*value.strip_prefix("a右"_str), "bc右"_str);
    EXPECT_EQ(*value.strip_suffix("右"_str), "a右bc"_str);
    EXPECT_TRUE(value.strip_prefix("右"_str).is_none());
    EXPECT_EQ(*value.strip_prefix(""_str), value);
    EXPECT_EQ(*value.strip_suffix(""_str), value);
}

TEST(Str, SplitOnceAndReverseFindPreserveEmptySides) {
    auto value = "left::middle::right"_str;

    auto first = value.split_once("::"_str);
    ASSERT_TRUE(first.is_some());
    EXPECT_EQ(first->template get<0>(), "left"_str);
    EXPECT_EQ(first->template get<1>(), "middle::right"_str);

    auto last = value.rsplit_once("::"_str);
    ASSERT_TRUE(last.is_some());
    EXPECT_EQ(last->template get<0>(), "left::middle"_str);
    EXPECT_EQ(last->template get<1>(), "right"_str);
    EXPECT_EQ(*value.rfind(""_str), value.size());

    auto empty_first = value.split_once(""_str).unwrap();
    EXPECT_EQ(empty_first.template get<0>(), ""_str);
    EXPECT_EQ(empty_first.template get<1>(), value);

    auto empty_last = value.rsplit_once(""_str).unwrap();
    EXPECT_EQ(empty_last.template get<0>(), value);
    EXPECT_EQ(empty_last.template get<1>(), ""_str);
}

TEST(Str, TrimAscii) {
    EXPECT_EQ("  hello  "_str.trim_ascii(), "hello"_str);
    EXPECT_EQ("\t\n\f hi \r\n"_str.trim_ascii(), "hi"_str);
    EXPECT_EQ(""_str.trim_ascii(), ""_str);
    EXPECT_EQ(" \t\n\f\r "_str.trim_ascii(), ""_str);
    EXPECT_EQ("\vtext\v"_str.trim_ascii(), "\vtext\v"_str);
    EXPECT_EQ("　text　"_str.trim_ascii(), "　text　"_str);
}

TEST(Str, SplitAt) {
    auto [a, b] = "hello"_str.split_at(usize(2));
    EXPECT_EQ(a, "he"_str);
    EXPECT_EQ(b, "llo"_str);

    auto [empty_left, whole] = "右"_str.split_at(usize());
    EXPECT_EQ(empty_left, ""_str);
    EXPECT_EQ(whole, "右"_str);
}

TEST(StrDeathTest, SplitAtRejectsNonBoundaryByteOffset) {
    EXPECT_DEATH("右"_str.split_at(usize(1)), "");
}

TEST(Str, BytesIsAnExactDoubleEndedIterator) {
    auto bytes = "a\0右"_str.bytes();

    EXPECT_EQ(bytes.len(), usize(5));
    EXPECT_EQ(bytes.next().unwrap(), u8('a'));
    EXPECT_EQ(bytes.next_back().unwrap(), u8(0xb3));
    EXPECT_EQ(bytes.len(), usize(3));

    auto remaining = rstd::move(bytes).collect<rstd::vec::Vec<u8>>();
    ASSERT_EQ(remaining.len(), usize(3));
    EXPECT_EQ(remaining[usize()], u8());
    EXPECT_EQ(remaining[usize(1)], u8(0xe5));
    EXPECT_EQ(remaining[usize(2)], u8(0x8f));

    auto empty = ""_str.bytes();
    EXPECT_TRUE(empty.next().is_none());
    EXPECT_TRUE(empty.next().is_none());
}

TEST(Str, CharsAscii) {
    std::vector<char32_t> cps;
    auto                  it = "ABC"_str.chars();
    for (auto c : it) cps.push_back(static_cast<char32_t>(c.to_primitive()));
    ASSERT_EQ(cps.size(), 3u);
    EXPECT_EQ(cps[0], U'A');
    EXPECT_EQ(cps[1], U'B');
    EXPECT_EQ(cps[2], U'C');
}

TEST(Str, CharsMultibyte) {
    // "中文" = 2 code points, 6 bytes
    std::vector<char32_t> cps;
    auto                  chars = "\xe4\xb8\xad\xe6\x96\x87"_str.chars();
    for (auto c : chars) cps.push_back(static_cast<char32_t>(c.to_primitive()));
    ASSERT_EQ(cps.size(), 2u);
    EXPECT_EQ(cps[0], char32_t(0x4E2D)); // 中
    EXPECT_EQ(cps[1], char32_t(0x6587)); // 文
}

TEST(Str, CharsEmoji) {
    // "😀" = 1 code point, 4 bytes
    std::vector<char32_t> cps;
    auto                  chars = "\xf0\x9f\x98\x80"_str.chars();
    for (auto c : chars) cps.push_back(static_cast<char32_t>(c.to_primitive()));
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
    auto chars = "é中x"_str.chars();
    EXPECT_EQ(chars.as_str(), "é中x"_str);
    EXPECT_EQ(chars.next().unwrap(), u32(U'é'));
    EXPECT_EQ(chars.as_str(), "中x"_str);
    EXPECT_EQ(chars.next().unwrap(), u32(U'中'));
    EXPECT_EQ(chars.as_str(), "x"_str);
    EXPECT_EQ(chars.next().unwrap(), u32(U'x'));
    EXPECT_TRUE(chars.next().is_none());
    EXPECT_TRUE(chars.next().is_none());
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

#include <gtest/gtest.h>

import rstd;

using rstd::as;
using rstd::to_string;
using namespace rstd::prelude;
using namespace rstd::literals;

template<typename T>
concept HasArrowAsciiLowercase = requires(T& value) { value->make_ascii_lowercase(); };

template<typename T>
concept HasDirectAsciiLowercase = requires(T& value) { value.make_ascii_lowercase(); };

static_assert(
    rstd::mtp::same_as<decltype(rstd::mtp::declval<String const&>().begin()), rstd::ptr<rstd::u8>>);
static_assert(rstd::mtp::same_as<rstd::ops::deref_target_t<String>, rstd::str>);
static_assert(rstd::Impled<String, rstd::ops::Deref>);
static_assert(rstd::Impled<String, rstd::ops::DerefMut>);
static_assert(HasArrowAsciiLowercase<String>);
static_assert(! HasArrowAsciiLowercase<String const>);
static_assert(! HasDirectAsciiLowercase<String>);
static_assert(
    rstd::mtp::same_as<decltype(*rstd::mtp::declval<String&>()), rstd::mut_ref<rstd::str>>);
static_assert(
    rstd::mtp::same_as<decltype(*rstd::mtp::declval<String const&>()), rstd::ref<rstd::str>>);
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<String&>().as_mut_str()),
                                 rstd::mut_ref<rstd::str>>);
static_assert(requires(String const& value) {
    { value->size() } -> rstd::mtp::same_as<usize>;
});

TEST(String, ToString) {
    int a = 10;

    auto a_str = to_string(a);

    EXPECT_EQ("10"_str, as<ToString>(a).to_string());
    EXPECT_EQ(a_str, a_str);
    EXPECT_EQ(a_str, "10"_str);
    EXPECT_EQ("10"_str, a_str);
}

TEST(String, CloneCopiesBytes) {
    auto original = String::make("hello"_str);
    auto direct   = original.clone();
    auto abstract = rstd::as<rstd::clone::Clone>(original).clone();

    original.push_ascii(u8('!'));

    EXPECT_EQ(original, "hello!"_str);
    EXPECT_EQ(direct, "hello"_str);
    EXPECT_EQ(abstract, "hello"_str);
}

TEST(String, RangeForYieldsUtf8CodeUnits) {
    auto text  = String::make("A右"_str);
    auto bytes = rstd::vec::Vec<rstd::u8>::make();
    for (auto value : text) bytes.push(u8(value.to_primitive()));

    ASSERT_EQ(bytes.len(), usize(4));
    EXPECT_EQ(bytes[usize()], u8('A'));
    EXPECT_EQ(bytes[usize(1)], u8(0xe5));
}

TEST(String, DerefMutMakesAsciiLowercaseInPlace) {
    auto text     = String::make("GRÜßE, JÜRGEN ❤\0Z"_str);
    auto data     = text.data();
    auto length   = text.len();
    auto capacity = text.capacity();

    text->make_ascii_lowercase();

    EXPECT_EQ(text, "grÜße, jÜrgen ❤\0z"_str);
    EXPECT_EQ(text.data(), data);
    EXPECT_EQ(text.len(), length);
    EXPECT_EQ(text.capacity(), capacity);
    EXPECT_TRUE(rstd::str_::validate_utf8(rstd::str_::as_bytes(text.as_str())).is_ok());

    text->make_ascii_lowercase();
    EXPECT_EQ(text, "grÜße, jÜrgen ❤\0z"_str);

    auto view = text.as_mut_str();
    view.make_ascii_lowercase();
    EXPECT_EQ(view.as_ref(), text.as_str());

    auto empty = String::make();
    empty->make_ascii_lowercase();
    EXPECT_TRUE(empty.is_empty());
}

TEST(String, BorrowedComparisonUsesAllBytes) {
    auto text = String::make("alpha"_str);

    EXPECT_LT(String::make(""_str), String::make("alpha"_str));
    EXPECT_LT(String::make("a"_str), String::make("alpha"_str));
    EXPECT_GT(String::make("alpha"_str), String::make("a"_str));
    EXPECT_EQ(text, "alpha"_str);
    EXPECT_EQ("alpha"_str, text);
    EXPECT_LT(text, "beta"_str);
    EXPECT_GT("beta"_str, text);

    auto owned = String::make("a\0b"_str);
    EXPECT_EQ(owned, "a\0b"_str);
    EXPECT_NE(owned, "a\0"_str);
}

TEST(String, PushStrAppendsCompleteSlice) {
    auto text = String::make("left"_str);
    text.push_str("-右"_str);
    EXPECT_EQ(text, "left-右"_str);
}

TEST(String, ReserveInsertAndReplaceRangeRespectUtf8Boundaries) {
    auto text = String::make("a右c"_str);
    text.reserve(usize(32));
    EXPECT_GE(text.capacity(), usize(32) + text.len());

    text.insert_str(usize(1), "中"_str);
    EXPECT_EQ(text, "a中右c"_str);

    text.insert(usize(4), U'!');
    EXPECT_EQ(text, "a中!右c"_str);

    text.replace_range(usize(1), usize(5), "文"_str);
    EXPECT_EQ(text, "a文右c"_str);
}

TEST(StringDeathTest, InsertRejectsNonBoundaryByteOffset) {
    auto text = String::make("a右c"_str);
    EXPECT_DEATH(text.insert_str(usize(2), "x"_str), "is_char_boundary");
}

TEST(String, FromUtf8OwnsValidatedBytes) {
    auto valid = rstd::vec::Vec<rstd::u8> {};
    valid.push(u8('a'));
    valid.push(u8(0xe5));
    valid.push(u8(0x8f));
    valid.push(u8(0xb3));
    valid.push(u8(0xef));
    valid.push(u8(0xbf));
    valid.push(u8(0xbd));

    auto text = String::from_utf8(rstd::move(valid));
    ASSERT_TRUE(text.is_ok());
    EXPECT_EQ(text.unwrap(), "a右�"_str);

    auto invalid = rstd::vec::Vec<rstd::u8> {};
    invalid.push(u8('a'));
    invalid.push(u8(0xff));
    auto error = String::from_utf8(rstd::move(invalid));
    ASSERT_TRUE(error.is_err());
    auto invalid_error = rstd::move(error).unwrap_err();
    EXPECT_EQ(invalid_error.utf8_error().valid_up_to(), usize(1));
    EXPECT_EQ(invalid_error.utf8_error().error_len(), Some(u8(1)));
    ASSERT_EQ(invalid_error.as_bytes().len(), usize(2));
    EXPECT_EQ(invalid_error.as_bytes()[usize()], u8('a'));
    EXPECT_EQ(invalid_error.as_bytes()[usize(1)], u8(0xff));

    auto incomplete = rstd::vec::Vec<rstd::u8> {};
    incomplete.push(u8('a'));
    incomplete.push(u8(0xe2));
    incomplete.push(u8(0x82));
    auto incomplete_error = String::from_utf8(rstd::move(incomplete));
    ASSERT_TRUE(incomplete_error.is_err());
    auto incomplete_utf8_error = incomplete_error.unwrap_err().utf8_error();
    EXPECT_EQ(incomplete_utf8_error.valid_up_to(), usize(1));
    EXPECT_TRUE(incomplete_utf8_error.error_len().is_none());

    auto overlong = rstd::vec::Vec<rstd::u8> {};
    overlong.push(u8('a'));
    overlong.push(u8(0xc0));
    overlong.push(u8(0xaf));
    auto overlong_error = String::from_utf8(rstd::move(overlong));
    ASSERT_TRUE(overlong_error.is_err());
    auto overlong_utf8_error = overlong_error.unwrap_err().utf8_error();
    EXPECT_EQ(overlong_utf8_error.valid_up_to(), usize(1));
    EXPECT_EQ(overlong_utf8_error.error_len(), Some(u8(1)));
}

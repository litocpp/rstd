#include <gtest/gtest.h>

import rstd;

using rstd::as;
using rstd::to_string;
using namespace rstd::prelude;

TEST(String, ToString) {
    int a = 10;

    auto a_str = to_string(a);

    EXPECT_EQ("10", as<ToString>(a).to_string());
    EXPECT_EQ(a_str, a_str);
    EXPECT_EQ(a_str, "10");
    EXPECT_EQ("10", a_str);
}

TEST(String, CloneCopiesBytes) {
    auto original = String::make("hello");
    auto direct   = original.clone();
    auto abstract = rstd::as<rstd::clone::Clone>(original).clone();

    original.push_back('!');

    EXPECT_EQ(original, "hello!");
    EXPECT_EQ(direct, "hello");
    EXPECT_EQ(abstract, "hello");
}

TEST(String, BorrowedComparisonUsesAllBytes) {
    auto text = String::make("alpha");

    EXPECT_LT(String::make(""), String::make("alpha"));
    EXPECT_LT(String::make("a"), String::make("alpha"));
    EXPECT_GT(String::make("alpha"), String::make("a"));
    EXPECT_EQ(text, rstd::ref<rstd::str>("alpha"));
    EXPECT_EQ(rstd::ref<rstd::str>("alpha"), text);
    EXPECT_LT(text, rstd::ref<rstd::str>("beta"));
    EXPECT_GT(rstd::ref<rstd::str>("beta"), text);

    const rstd::uint8_t embedded[] = { 'a', 0, 'b' };
    auto                owned      = String::make(rstd::ref<rstd::str>(embedded, usize(3)));
    EXPECT_EQ(owned, rstd::ref<rstd::str>(embedded, usize(3)));
    EXPECT_NE(owned, rstd::ref<rstd::str>(embedded, usize(2)));
}

TEST(String, PushStrAppendsCompleteSlice) {
    auto text = String::make("left");
    text.push_str("-右");
    EXPECT_EQ(text, rstd::ref<rstd::str>("left-右"));
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
    EXPECT_EQ(text.unwrap(), rstd::ref<rstd::str>("a右�"));

    auto invalid = rstd::vec::Vec<rstd::u8> {};
    invalid.push(u8('a'));
    invalid.push(u8(0xff));
    auto error = String::from_utf8(rstd::move(invalid));
    ASSERT_TRUE(error.is_err());
    EXPECT_EQ(error.unwrap_err().valid_up_to(), usize(1));

    auto incomplete = rstd::vec::Vec<rstd::u8> {};
    incomplete.push(u8('a'));
    incomplete.push(u8(0xe2));
    incomplete.push(u8(0x82));
    auto incomplete_error = String::from_utf8(rstd::move(incomplete));
    ASSERT_TRUE(incomplete_error.is_err());
    EXPECT_EQ(incomplete_error.unwrap_err().valid_up_to(), usize(1));

    auto overlong = rstd::vec::Vec<rstd::u8> {};
    overlong.push(u8('a'));
    overlong.push(u8(0xc0));
    overlong.push(u8(0xaf));
    auto overlong_error = String::from_utf8(rstd::move(overlong));
    ASSERT_TRUE(overlong_error.is_err());
    EXPECT_EQ(overlong_error.unwrap_err().valid_up_to(), usize(1));
}

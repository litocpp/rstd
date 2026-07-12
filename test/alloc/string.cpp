#include <gtest/gtest.h>

import rstd;

using rstd::as;
using rstd::to_string;
using rstd::string::String;
using rstd::string::ToString;

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

    EXPECT_EQ(text, rstd::ref<rstd::str>("alpha"));
    EXPECT_EQ(rstd::ref<rstd::str>("alpha"), text);
    EXPECT_LT(text, rstd::ref<rstd::str>("beta"));
    EXPECT_GT(rstd::ref<rstd::str>("beta"), text);

    const rstd::u8 embedded[] = { 'a', 0, 'b' };
    auto           owned      = String::make(rstd::ref<rstd::str>(embedded, 3));
    EXPECT_EQ(owned, rstd::ref<rstd::str>(embedded, 3));
    EXPECT_NE(owned, rstd::ref<rstd::str>(embedded, 2));
}

TEST(String, PushStrAppendsCompleteSlice) {
    auto text = String::make("left");
    text.push_str("-右");
    EXPECT_EQ(text, rstd::ref<rstd::str>("left-右"));
}

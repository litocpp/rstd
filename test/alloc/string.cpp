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
    auto cloned   = rstd::as<rstd::clone::Clone>(original).clone();

    original.push_back('!');

    EXPECT_EQ(original, "hello!");
    EXPECT_EQ(cloned, "hello");
}

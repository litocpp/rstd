#include <gtest/gtest.h>

import rstd;

using namespace rstd;

TEST(String, ToString) {
    int a = 10;

    EXPECT_EQ("10", as<string::ToString>(a).to_string());
    EXPECT_EQ("10", to_string(a));
}

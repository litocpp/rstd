#include <gtest/gtest.h>
import rstd.core;
using rstd::num::nonzero::NonZero;
using namespace rstd;

TEST(NonZero, Basic) {
    auto non = NonZero<u32>::make(0);
    auto ok  = NonZero<u32>::make(1);

    EXPECT_FALSE(non);
    EXPECT_TRUE(ok);
    EXPECT_EQ(ok.unwrap().get(), 1);
}
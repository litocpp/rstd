#include <gtest/gtest.h>

import rstd;

struct B : rstd::WithTrait<B, rstd::clone::Clone> {
    int a;
    B(int v): a(v) {}
    B(const B& o): a(o.a) {}
};

TEST(Clone, Auto) {
    B b { 1 };
    auto b2 = b.clone();
    EXPECT_EQ(b2.a, 1);
}
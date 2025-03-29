#include <gtest/gtest.h>
#include <tuple>

import rstd;

struct B : rstd::WithTrait<B, rstd::clone::Clone> {
    int a;
    B(int v): a(v) {}
    B(const B& o): a(o.a) {}
};

TEST(Clone, Auto) {
    B    b { 1 };
    auto b2 = b.clone();
    EXPECT_EQ(b2.a, 1);
}

TEST(Clone, tuple) {
    std::tuple t { 1, 1.5, B { 11 } };
    auto       t2 = rstd::Impl<rstd::clone::Clone, decltype(t)>{&t}.clone();
    EXPECT_EQ(std::get<2>(t2), B { 11 });
}
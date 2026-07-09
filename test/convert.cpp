#include <gtest/gtest.h>
#include <tuple>

import rstd;

struct A {
    int a;
};

struct B {
    int a;
};

struct C;

template<>
struct rstd::Impl<rstd::convert::From<C>, A> {
    static auto from(C c) -> A;
};

template<>
struct rstd::Impl<rstd::convert::From<C>, B> {
    static auto from(C c) -> B;
};
struct C {
    int a;
};

auto rstd::Impl<rstd::convert::From<C>, A>::from(C c) -> A { return { c.a }; }
auto rstd::Impl<rstd::convert::From<C>, B>::from(C c) -> B { return { c.a }; }

struct D {
    int a;
    auto into() -> A { return { a }; }
};

template<>
struct rstd::Impl<rstd::convert::Into<A>, D> : rstd::LinkClassMethod<rstd::convert::Into<A>, D> {};

template<>
struct rstd::Impl<rstd::convert::From<B>, A> {
    using from_t = B;
    using Self   = A;
    static auto from(from_t b) -> Self { return { b.a }; }
};

TEST(Convert, Basic) {
    B    b { 100 };
    auto a = rstd::Impl<rstd::convert::From<B>, A>::from(b);
    EXPECT_EQ(b.a, a.a);
    a.a = 0;
    a   = rstd::as<rstd::convert::Into<A>>(b).into();
    EXPECT_EQ(b.a, a.a);
    C c {};
    c.a = 999;
    a   = rstd::into(c);
    b   = rstd::into(c);
    EXPECT_EQ(c.a, a.a);
    EXPECT_EQ(c.a, b.a);
    D d { 321 };
    a = d.into();
    EXPECT_EQ(a.a, 321);
    D e { 654 };
    a = rstd::as<rstd::convert::Into<A>>(e).into();
    EXPECT_EQ(a.a, 654);
}

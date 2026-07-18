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

auto rstd::Impl<rstd::convert::From<C>, A>::from(C c) -> A {
    return { c.a };
}
auto rstd::Impl<rstd::convert::From<C>, B>::from(C c) -> B {
    return { c.a };
}

struct D {
    int  a;
    auto into() -> A { return { a }; }
};

struct Checked {
    int value;
};

struct ConvertError {
    int value;
};

struct TryIntoOnly {
    int value;
};

template<>
struct rstd::Impl<rstd::convert::From<B>, A> {
    using from_t = B;
    using Self   = A;
    static auto from(from_t b) -> Self { return { b.a }; }
};

template<>
struct rstd::Impl<rstd::convert::TryFrom<int>, Checked> {
    using Error = ConvertError;

    static auto try_from(int value) -> rstd::Result<Checked, Error> {
        if (value < 0) return rstd::Err(Error { value });
        return rstd::Ok(Checked { value });
    }
};

template<>
struct rstd::Impl<rstd::convert::TryInto<Checked>, TryIntoOnly> : rstd::ImplBase<TryIntoOnly> {
    using Error = ConvertError;

    auto try_into() -> rstd::Result<Checked, Error> {
        auto value = this->self().value;
        if (value < 0) return rstd::Err(Error { value });
        return rstd::Ok(Checked { value });
    }
};

static_assert(rstd::Impled<Checked, rstd::convert::TryFrom<int>>);
static_assert(rstd::Impled<int, rstd::convert::TryInto<Checked>>);
static_assert(rstd::Impled<TryIntoOnly, rstd::convert::TryInto<Checked>>);
static_assert(! rstd::Impled<Checked, rstd::convert::TryFrom<TryIntoOnly>>);

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

TEST(Convert, TryFromAndTryIntoShareTargetImplementation) {
    auto from = rstd::try_from<Checked>(42).unwrap();
    EXPECT_EQ(from.value, 42);

    int  value = 7;
    auto into  = rstd::try_into<Checked>(value).unwrap();
    EXPECT_EQ(into.value, 7);

    auto error = rstd::try_into<Checked>(-3).unwrap_err();
    EXPECT_EQ(error.value, -3);

    auto direct = rstd::try_into<Checked>(TryIntoOnly { 19 }).unwrap();
    EXPECT_EQ(direct.value, 19);
}

TEST(Convert, InfallibleAndIdentityConversionsUseTryFrom) {
    using FromResult = decltype(rstd::try_from<A>(B {}));
    static_assert(rstd::mtp::same_as<FromResult, rstd::Result<A, rstd::convert::Infallible>>);

    auto converted = rstd::try_into<A>(B { 91 }).unwrap();
    EXPECT_EQ(converted.a, 91);

    using IdentityResult = decltype(rstd::try_from<A>(A {}));
    static_assert(rstd::mtp::same_as<IdentityResult, rstd::Result<A, rstd::convert::Infallible>>);
    EXPECT_EQ(rstd::try_from<A>(A { 12 }).unwrap().a, 12);
}

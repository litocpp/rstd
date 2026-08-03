#include <rstd/test/gtest.hpp>
#include <tuple>

import rstd;

using namespace rstd::prelude;

struct UnmarkedTrivial {
    int value;
};

struct MarkedCopy {
    int value;
};

struct CopyWithoutClone {
    int value;
};

namespace rstd
{

template<>
struct Impl<Copy, ::MarkedCopy> {};

template<>
struct Impl<clone::Clone, ::CopyWithoutClone> {
    ~Impl() = delete;
};

template<>
struct Impl<Copy, ::CopyWithoutClone> {};

} // namespace rstd

template<typename... Ts>
constexpr bool ALL_COPY = (rstd::Impled<Ts, rstd::Copy> && ...);

using FunctionPointer = void (*)();

static_assert(ALL_COPY<bool,
                       char,
                       signed char,
                       unsigned char,
                       wchar_t,
                       char8_t,
                       char16_t,
                       char32_t,
                       short,
                       unsigned short,
                       int,
                       unsigned int,
                       long,
                       unsigned long,
                       long long,
                       unsigned long long,
                       float,
                       double,
                       long double>);
static_assert(ALL_COPY<u8, u16, u32, u64, u128, usize, i8, i16, i32, i64, i128, isize, f32, f64>);
static_assert(ALL_COPY<rstd::byte, int*, int const*, FunctionPointer>);
static_assert(ALL_COPY<rstd::ptr<int>, rstd::mut_ptr<int>, rstd::ref<int>, rstd::slice<int>>);
static_assert(rstd::Impled<int, Copy>);
static_assert(rstd::Impled<rstd::ref<rstd::str>, rstd::Copy>);
static_assert(! rstd::Impled<rstd::mut_ref<int>, rstd::Copy>);
static_assert(rstd::Impled<rstd::array<int, 3>, rstd::Copy>);
static_assert(rstd::Impled<rstd::array<int, 0>, rstd::Copy>);
static_assert(rstd::Impled<rstd::array<MarkedCopy, 1>, rstd::Copy>);
static_assert(rstd::Impled<rstd::tuple<int, f64>, rstd::Copy>);
static_assert(rstd::Impled<rstd::tuple<>, rstd::Copy>);
static_assert(rstd::Impled<rstd::tuple<MarkedCopy>, rstd::Copy>);
static_assert(! rstd::Impled<rstd::array<UnmarkedTrivial, 1>, rstd::Copy>);
static_assert(! rstd::Impled<rstd::tuple<int, UnmarkedTrivial>, rstd::Copy>);
static_assert(! rstd::Impled<rstd::array<rstd::prelude::String, 1>, rstd::Copy>);
static_assert(! rstd::Impled<rstd::tuple<int, rstd::prelude::String>, rstd::Copy>);
static_assert(! rstd::Impled<UnmarkedTrivial, rstd::Copy>);
static_assert(rstd::Impled<MarkedCopy, rstd::Copy>);
static_assert(! rstd::Impled<CopyWithoutClone, rstd::clone::Clone>);
static_assert(! rstd::Impled<CopyWithoutClone, rstd::Copy>);
static_assert(! rstd::Impled<rstd::prelude::String, rstd::Copy>);
static_assert(! rstd::Impled<rstd::prelude::Vec<int>, rstd::Copy>);

TEST(Copy, ExplicitMarkerEnablesSliceCopy) {
    MarkedCopy source[] { { 3 }, { 7 } };
    MarkedCopy destination[] { {}, {} };

    rstd::slice_::copy_from_slice(
        rstd::mut_ref<MarkedCopy[]>::from_raw_parts(destination, usize(2)),
        rstd::slice<MarkedCopy>::from_raw_parts(source, usize(2)));

    EXPECT_EQ(destination[0].value, 3);
    EXPECT_EQ(destination[1].value, 7);
}

struct B : rstd::DefaultInClass<B, rstd::clone::Clone> {
    int a;

    B(int v): a(v) {}
    B(const B& o): a(o.a) {}
    B&   operator=(const B& o) = default;
    auto clone() const -> B { return B { *this }; }
    auto operator==(const B& other) const -> bool { return a == other.a; }
};

TEST(Clone, Auto) {
    B    b { 1 };
    auto b2 = b.clone();

    auto b3 = rstd::as<rstd::clone::Clone>(b2).clone();
    EXPECT_EQ(b2.a, 1);
    EXPECT_EQ(b3.a, 1);
}

TEST(Clone, tuple) {
    std::tuple t { 1, 1.5, B { 11 } };
    auto       t2 = rstd::Impl<rstd::clone::Clone, decltype(t)> { &t }.clone();
    EXPECT_EQ(std::get<2>(t2), B { 11 });
}

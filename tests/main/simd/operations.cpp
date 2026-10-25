#include <rstd/test/gtest.hpp>

import rstd.core;

using namespace rstd;
using simd::Simd;

template<typename T, size_t N>
void integer_extended() {
    SCOPED_TRACE(__PRETTY_FUNCTION__);
    using V = Simd<T, N>;
    array<T, N>      a {}, b {};
    array<u64, N>    shifts {};
    constexpr size_t width = sizeof(typename T::primitive_type) * 8;
    const T          seeds[] { T::MIN, T::MAX, T(0), T(1), as_cast<T>(i64(-1)), T(3) };
    const u64        counts[] { u64(0), u64(width - 1), u64(width), u64(width + 1), u64::MAX };
    for (size_t round = 0; round < 30; ++round) {
        for (size_t i = 0; i < N; ++i) {
            a[usize(i)]      = seeds[(i + round) % 6];
            b[usize(i)]      = seeds[(i + round / 6) % 6];
            shifts[usize(i)] = counts[(i + round) % 5];
        }
        auto x = V::from_array(a), y = V::from_array(b);
        auto c    = Simd<u64, N>::from_array(shifts);
        auto left = x.wrapping_shl(c), right = x.wrapping_shr(c);
        auto scalar_left  = x.wrapping_shl(counts[round % 5]);
        auto scalar_right = x.wrapping_shr(counts[round % 5]);
        auto small_count  = x.wrapping_shr(c.template cast<u8>());
        T    lo = a[usize(0)], hi = lo, ands = lo, ors = lo, xors {};
        for (size_t i = 0; i < N; ++i) {
            T v = a[usize(i)], w = b[usize(i)];
            ASSERT_TRUE((left[usize(i)] == v.wrapping_shl(shifts[usize(i)])));
            ASSERT_TRUE((right[usize(i)] == v.wrapping_shr(shifts[usize(i)])));
            ASSERT_TRUE((scalar_left[usize(i)] == v.wrapping_shl(counts[round % 5])));
            ASSERT_TRUE((scalar_right[usize(i)] == v.wrapping_shr(counts[round % 5])));
            ASSERT_TRUE((small_count[usize(i)] == right[usize(i)]));
            ASSERT_TRUE((x.saturating_add(y)[usize(i)] == v.saturating_add(w)));
            ASSERT_TRUE((x.saturating_sub(y)[usize(i)] == v.saturating_sub(w)));
            ASSERT_TRUE((x.wrapping_neg()[usize(i)] == v.wrapping_neg()));
            if constexpr (T::IS_SIGNED) {
                ASSERT_TRUE((x.wrapping_abs()[usize(i)] == (v < T(0) ? v.wrapping_neg() : v)));
            }
            ASSERT_TRUE((x.min(y)[usize(i)] == v.min(w)));
            ASSERT_TRUE((x.max(y)[usize(i)] == v.max(w)));
            lo   = lo.min(v);
            hi   = hi.max(v);
            ands = ands & v;
            ors  = ors | v;
            xors = xors ^ v;
        }
        ASSERT_TRUE((x.reduce_min() == lo && x.reduce_max() == hi));
        ASSERT_TRUE((x.reduce_and() == ands && x.reduce_or() == ors && x.reduce_xor() == xors));
    }
}

template<typename T>
auto same_float(T a, T b) -> bool {
    return (a.is_nan() && b.is_nan()) || a.to_bits() == b.to_bits();
}

template<typename T, size_t N>
void floating_extended() {
    SCOPED_TRACE(__PRETTY_FUNCTION__);
    using V = Simd<T, N>;
    using B = typename V::bits_type;
    const T seeds[] { T(0),
                      T(-0.0),
                      T(1),
                      T(-2),
                      T(__builtin_inf()),
                      T(-__builtin_inf()),
                      T(__builtin_nan("")),
                      T::from_bits(B(1)),
                      T::MIN_POSITIVE,
                      T::MAX };
    for (size_t round = 0; round < 10; ++round) {
        array<T, N> a {}, b {};
        for (size_t i = 0; i < N; ++i) {
            a[usize(i)] = seeds[(i + round) % 10];
            b[usize(i)] = seeds[(i + round + 3) % 10];
        }
        auto x = V::from_array(a), y = V::from_array(b);
        ASSERT_TRUE((V::from_bits(x.to_bits()).to_bits() == x.to_bits()));
        for (size_t i = 0; i < N; ++i) {
            T v = a[usize(i)], w = b[usize(i)];
            ASSERT_TRUE((same_float((x / y)[usize(i)], v / w)));
            ASSERT_TRUE((same_float((-x)[usize(i)], -v)));
            ASSERT_TRUE((same_float(x.abs()[usize(i)], v.abs())));
            ASSERT_TRUE((same_float(x.copysign(y)[usize(i)], v.copysign(w))));
            auto lo = x.min(y)[usize(i)], hi = x.max(y)[usize(i)];
            ASSERT_TRUE(((lo.is_nan() && v.min(w).is_nan()) || lo == v.min(w)));
            ASSERT_TRUE(((hi.is_nan() && v.max(w).is_nan()) || hi == v.max(w)));
#define CLASSIFY(name) ASSERT_TRUE((x.name()[usize(i)] == v.name()))
            CLASSIFY(is_nan);
            CLASSIFY(is_infinite);
            CLASSIFY(is_finite);
            CLASSIFY(is_normal);
            CLASSIFY(is_subnormal);
            CLASSIFY(is_sign_negative);
            CLASSIFY(is_sign_positive);
#undef CLASSIFY
        }
    }
    ASSERT_TRUE((V::splat(T(-0.0)).reduce_sum_unordered().is_sign_negative()));
    ASSERT_TRUE((V::splat(T(2)).reduce_sum_unordered() == T(2 * N)));
    ASSERT_TRUE((V::splat(T(__builtin_nan(""))).reduce_min().is_nan()));
    ASSERT_TRUE((V::splat(T(__builtin_nan(""))).reduce_max().is_nan()));
    if constexpr (N > 1) {
        auto mixed = V::splat(T(__builtin_nan(""))).with_lane(usize(N - 1), T(7));
        ASSERT_TRUE((mixed.reduce_min() == T(7) && mixed.reduce_max() == T(7)));
        auto zeros = V::splat(T(-0.0)).with_lane(usize(0), T(0));
        ASSERT_TRUE((zeros.reduce_min() == T(0) && zeros.reduce_max() == T(0)));
        auto inf = V::splat(T(0))
                       .with_lane(usize(0), T(__builtin_inf()))
                       .with_lane(usize(1), T(-__builtin_inf()));
        ASSERT_TRUE((inf.reduce_sum_unordered().is_nan()));
    }
    auto a = V::splat(T(1) + T::EPSILON), b = V::splat(T(1) - T::EPSILON);
    rstd::hint::black_box(a);
    rstd::hint::black_box(b);
    auto fused = a.mul_add(b, V::splat(T(-1)));
    ASSERT_TRUE((fused == V::splat(-(T::EPSILON * T::EPSILON))));
    auto product = a * b;
    rstd::hint::black_box(product);
    ASSERT_TRUE((product + V::splat(T(-1)) == V::splat(T(0))));
    auto exceptional = V::splat(T(0)).mul_add(V::splat(T(__builtin_inf())), V::splat(T(1)));
    ASSERT_TRUE((exceptional.is_nan().all()));
}

template<typename T, size_t N>
void rearrange() {
    SCOPED_TRACE(__PRETTY_FUNCTION__);
    using V = Simd<T, N>;
    array<T, N> a {}, b {};
    for (size_t i = 0; i < N; ++i) {
        a[usize(i)] = T(i);
        b[usize(i)] = T(N + i);
    }
    auto x = V::from_array(a), y = V::from_array(b);
    auto [low, high] = x.interleave(y);
    for (size_t i = 0; i < N; ++i) {
        ASSERT_TRUE((low[usize(i)] == T(i / 2 + (i % 2) * N)));
        ASSERT_TRUE((high[usize(i)] == T((i + N) / 2 + ((i + N) % 2) * N)));
    }
    auto [xx, yy] = low.deinterleave(high);
    ASSERT_TRUE((xx == x && yy == y));
    if constexpr (N <= 32) {
        auto merged = x.concat(y);
        for (size_t i = 0; i < 2 * N; ++i) ASSERT_TRUE((merged[usize(i)] == T(i)));
        auto [first, second] = merged.split();
        ASSERT_TRUE((first == x && second == y));
    }
    auto repeated = x.template swizzle<0, 0, 0, 0>();
    ASSERT_TRUE((repeated == Simd<T, 4>::splat(T(0))));
    auto edges = x.template swizzle<N - 1, N, 0, 2 * N - 1>(y);
    ASSERT_TRUE((edges[usize(0)] == T(N - 1) && edges[usize(1)] == T(N)));
    ASSERT_TRUE((edges[usize(2)] == T(0) && edges[usize(3)] == T(2 * N - 1)));
}

template<typename From, typename To>
void conversion() {
    SCOPED_TRACE(__PRETTY_FUNCTION__);
    using V = Simd<From, 4>;
    array<From, 8> input {};
    input[usize(0)] = From(0);
    input[usize(1)] = From(1);
    input[usize(2)] = From::MIN;
    input[usize(3)] = From::MAX;
    if constexpr (simd::FloatElement<From>) {
        input[usize(4)] = From(__builtin_nan(""));
        input[usize(5)] = From(__builtin_inf());
        input[usize(6)] = From(-__builtin_inf());
        input[usize(7)] = From(-1.75);
    } else {
        input[usize(4)] = as_cast<From>(i64(-1));
        input[usize(5)] = From(127);
        input[usize(6)] = From(17);
        input[usize(7)] = From(3);
    }
    for (size_t part = 0; part < 2; ++part) {
        auto x = V::from_slice(
            slice<From>::from_raw_parts(input.as_slice().as_raw_ptr() + part * 4, usize(4)));
        auto y = x.template cast<To>();
        for (size_t i = 0; i < 4; ++i) {
            From value    = input[usize(part * 4 + i)];
            auto expected = as_cast<To>(value);
            if constexpr (simd::FloatElement<To>) {
                ASSERT_TRUE((same_float(y[usize(i)], expected)));
            } else {
                ASSERT_TRUE((y[usize(i)] == expected));
            }
        }
    }
}

template<typename From>
void conversions() {
    conversion<From, u8>();
    conversion<From, i8>();
    conversion<From, u16>();
    conversion<From, i16>();
    conversion<From, u32>();
    conversion<From, i32>();
    conversion<From, u64>();
    conversion<From, i64>();
    conversion<From, usize>();
    conversion<From, isize>();
    conversion<From, f32>();
    conversion<From, f64>();
}

template<typename F, typename I>
void float_integer_boundaries() {
    using V              = Simd<F, 4>;
    constexpr int digits = sizeof(typename I::primitive_type) * 8 - (I::IS_SIGNED ? 1 : 0);
    auto          upper  = F(__builtin_ldexp(1.0, digits));
    auto          below =
        F(upper.to_primitive() * (1.0 - typename F::primitive_type(F::EPSILON.to_primitive())));
    array<F, 4> input { upper, below, -upper, -below };
    auto        converted = V::from_array(input).template cast<I>();
    for (size_t i = 0; i < 4; ++i)
        ASSERT_TRUE((converted[usize(i)] == as_cast<I>(input[usize(i)])));
    ASSERT_TRUE((converted[usize(0)] == I::MAX));
}

template<typename T>
void widths() {
    [&]<size_t... K>(mtp::index_sequence<K...>) {
        (([] {
             constexpr size_t N = size_t(1) << K;
             if constexpr (simd::IntegerElement<T>)
                 integer_extended<T, N>();
             else
                 floating_extended<T, N>();
             rearrange<T, N>();
         }()),
         ...);
    }(mtp::make_index_sequence<7> {});
    conversions<T>();
}

TEST(Simd, U8OperationsAndConversions) {
    widths<u8>();
}
TEST(Simd, I8OperationsAndConversions) {
    widths<i8>();
}
TEST(Simd, U16OperationsAndConversions) {
    widths<u16>();
}
TEST(Simd, I16OperationsAndConversions) {
    widths<i16>();
}
TEST(Simd, U32OperationsAndConversions) {
    widths<u32>();
}
TEST(Simd, I32OperationsAndConversions) {
    widths<i32>();
}
TEST(Simd, U64OperationsAndConversions) {
    widths<u64>();
}
TEST(Simd, I64OperationsAndConversions) {
    widths<i64>();
}
TEST(Simd, UsizeOperationsAndConversions) {
    widths<usize>();
}
TEST(Simd, IsizeOperationsAndConversions) {
    widths<isize>();
}
TEST(Simd, F32OperationsAndConversions) {
    widths<f32>();
}
TEST(Simd, F64OperationsAndConversions) {
    widths<f64>();
}

TEST(Simd, FloatIntegerBoundaries) {
    float_integer_boundaries<f32, i32>();
    float_integer_boundaries<f32, u32>();
    float_integer_boundaries<f32, i64>();
    float_integer_boundaries<f32, u64>();
    float_integer_boundaries<f64, i32>();
    float_integer_boundaries<f64, u32>();
    float_integer_boundaries<f64, i64>();
    float_integer_boundaries<f64, u64>();
}

TEST(Simd, OrderedAndUnorderedReduction) {
    array<f32, 4> v { f32(1e20), f32(1), f32(-1e20), f32(3) };
    auto          x = Simd<f32, 4>::from_array(v);
    ASSERT_TRUE((x.reduce_sum_ordered() == f32(3)));
    ASSERT_TRUE((x.reduce_sum_unordered() == f32(0)));
}

TEST(Simd, BitRepresentation) {
    auto bits = Simd<u32, 4>::splat(u32(0x80000000)).bit_cast<f32>();
    ASSERT_TRUE((bits.is_sign_negative().all()));
    ASSERT_TRUE((bits.to_bits() == Simd<u32, 4>::splat(u32(0x80000000))));
}

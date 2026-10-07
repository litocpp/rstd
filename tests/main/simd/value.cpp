#include <rstd/test/gtest.hpp>
#if defined(__unix__)
#include <sys/mman.h>
#include <unistd.h>
#endif

import rstd.core;

using namespace rstd;
using simd::Simd;
using simd::Mask;

static_assert(simd::Element<u8> && simd::Element<f64>);
static_assert(! simd::Element<float> && ! simd::Element<u128>);
static_assert(simd::LaneCount<1> && simd::LaneCount<64>);
static_assert(! simd::LaneCount<0> && ! simd::LaneCount<3> && ! simd::LaneCount<128>);
static_assert(Simd<u8, 16>::LANES == usize(16));

template<typename T, size_t N>
void memory_and_masks() {
    using V = Simd<T, N>;
    array<T, N + 2> input {};
    array<T, N + 2> output {};
    for (size_t i = 0; i < N + 2; ++i) input[usize(i)] = T(i + 1);
    for (size_t len = 0; len <= N + 1; ++len) {
        auto source = slice<T>::from_raw_parts(input.as_slice().as_raw_ptr() + 1, usize(len));
        auto v      = V::load_or(source, V::splat(T(99)));
        auto m      = Mask<N>::first_n(usize(len));
        ASSERT_TRUE((m.count() == usize(len < N ? len : N)));
        ASSERT_TRUE((m.any() == (len != 0)));
        ASSERT_TRUE((m.all() == (len >= N)));
        ASSERT_TRUE((m.first_set().is_some() == (len != 0)));
        if (len) {
            ASSERT_TRUE((m.first_set().unwrap() == usize(0)));
        }
        auto picked = m.select(v, V::splat(T(77)));
        for (size_t i = 0; i < N; ++i) {
            ASSERT_TRUE((v[usize(i)] == T(i < len ? i + 2 : 99)));
            ASSERT_TRUE((picked[usize(i)] == T(i < len ? i + 2 : 77)));
            ASSERT_TRUE((m[usize(i)] == (i < len)));
        }
        for (size_t i = 0; i < N + 2; ++i) output[usize(i)] = T(55);
        v.store_partial(
            mut_ref<T[]>::from_raw_parts(output.as_mut_slice().as_raw_ptr() + 1, usize(len)));
        ASSERT_TRUE((output[usize(0)] == T(55)));
        for (size_t i = 0; i < N + 1; ++i)
            ASSERT_TRUE((output[usize(i + 1)] == (i < len && i < N ? T(i + 2) : T(55))));
        ASSERT_TRUE(((m | ~m).all()));
        ASSERT_TRUE((! (m & ~m).any()));
        ASSERT_TRUE((m == ~~m));
    }
    auto v = V::from_slice(input.as_slice());
    ASSERT_TRUE((V::from_array(v.to_array()) == v));
    ASSERT_TRUE((v.with_lane(usize(N - 1), T(42))[usize(N - 1)] == T(42)));
    for (size_t i = 0; i < N; ++i) {
        auto hit = V().with_lane(usize(i), T(1)).simd_eq(V::splat(T(1)));
        ASSERT_TRUE((hit.count() == usize(1)));
        ASSERT_TRUE((hit.first_set().unwrap() == usize(i)));
    }
    ASSERT_TRUE((v.simd_lt(V::splat(T(N + 1))).all()));
    ASSERT_TRUE((v.simd_le(v).all() && v.simd_ge(v).all()));
    ASSERT_TRUE((! v.simd_gt(v).any() && ! v.simd_ne(v).any()));
}

template<typename T, size_t N>
void integer_ops() {
    using V  = Simd<T, N>;
    auto max = V::splat(T::MAX);
    auto one = V::splat(T(1));
    ASSERT_TRUE(((max + one) == V::splat(T::MAX.wrapping_add(T(1)))));
    ASSERT_TRUE(((V::splat(T::MIN) - one) == V::splat(T::MIN.wrapping_sub(T(1)))));
    ASSERT_TRUE(((max * V::splat(T(3))) == V::splat(T::MAX.wrapping_mul(T(3)))));
    ASSERT_TRUE(((max & one) == one));
    ASSERT_TRUE((((max ^ one) | one) == max));
    ASSERT_TRUE(((~V()) == V::splat(~T())));
    T sum {};
    for (size_t i = 0; i < N; ++i) sum = sum.wrapping_add(T::MAX);
    ASSERT_TRUE((max.reduce_sum() == sum));
    memory_and_masks<T, N>();
}

template<typename T>
void floats() {
    using V = Simd<T, 4>;
    array<T, 4> values { T(1e20), T(1), T(-1e20), T(3) };
    auto        v = V::from_array(values);
    ASSERT_TRUE((v.reduce_sum_ordered() == T(3)));
    ASSERT_TRUE((V::splat(T(2)).reduce_sum_ordered(T(1)) == T(9)));
    ASSERT_TRUE(
        ((V::splat(T(2)) * V::splat(T(3)) + V::splat(T(1)) - V::splat(T(2))) == V::splat(T(5))));
    auto nan = V::splat(T(__builtin_nan("")));
    ASSERT_TRUE((! nan.simd_eq(nan).any()));
    ASSERT_TRUE((nan.simd_ne(nan).all()));
    ASSERT_TRUE((! nan.simd_lt(v).any() && ! nan.simd_le(v).any()));
    auto zero = V::splat(T(-0.0));
    ASSERT_TRUE((zero == V()));
    ASSERT_TRUE((Mask<4>::splat(true).select(zero, V())[usize(0)].is_sign_negative()));
    ASSERT_TRUE((V::splat(T(__builtin_inf())).simd_gt(v).all()));
    memory_and_masks<T, 4>();
}

void search_cases() {
    array<u8, 160> bytes {};
    for (size_t offset = 0; offset < 32; ++offset) {
        for (size_t n = 0; n <= 96; ++n) {
            auto data = slice<u8>::from_raw_parts(bytes.as_slice().as_raw_ptr() + offset, usize(n));
            for (size_t i = 0; i < 160; ++i) bytes[usize(i)] = u8(7);
            ASSERT_TRUE((memchr::memchr(u8(0), data).is_none()));
            ASSERT_TRUE((memchr::memchr(u8(7), data).is_some() == (n != 0)));
            for (size_t hit = 0; hit < n; ++hit) {
                bytes[usize(offset + hit)] = u8(0);
                ASSERT_TRUE((memchr::memchr(u8(0), data).unwrap() == usize(hit)));
                bytes[usize(offset + hit)] = u8(7);
            }
            if (n > 1) {
                bytes[usize(offset)]         = u8(0);
                bytes[usize(offset + n - 1)] = u8(0);
                ASSERT_TRUE((memchr::memchr(u8(0), data).unwrap() == usize(0)));
            }
        }
    }
}

#if defined(__unix__)
void guard_pages() {
    auto page_size = sysconf(_SC_PAGESIZE);
    ASSERT_GT(page_size, 0);
    auto  page = static_cast<size_t>(page_size);
    auto* raw  = static_cast<unsigned char*>(
        mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    ASSERT_TRUE((raw != MAP_FAILED));
    struct Mapping {
        void*  address;
        size_t length;
        ~Mapping() { EXPECT_EQ(munmap(address, length), 0); }
    } mapping { raw, page * 2 };
    ASSERT_EQ(mprotect(raw + page, page, PROT_NONE), 0);
    auto* bytes = reinterpret_cast<byte*>(raw);
    for (size_t n = 0; n <= 64; ++n) {
        for (size_t i = 0; i < n; ++i) raw[page - n + i] = 7;
        auto s = slice<u8>::from_raw_parts(bytes + page - n, usize(n));
        auto v = Simd<u8, 64>::load_or(s, Simd<u8, 64>::splat(u8(99)));
        ASSERT_TRUE((v[usize(63)] == u8(n == 64 ? 7 : 99)));
        ASSERT_TRUE((memchr::memchr(u8(0), s).is_none()));
        v.store_partial(mut_ref<u8[]>::from_raw_parts(bytes + page - n, usize(n)));
        if (n) {
            raw[page - 1] = 0;
            ASSERT_TRUE((memchr::memchr(u8(0), s).unwrap() == usize(n - 1)));
        }
    }
}

#endif

TEST(Simd, IntegerArithmeticAndMasks) {
    integer_ops<u8, 1>();
    integer_ops<u8, 2>();
    integer_ops<u8, 4>();
    integer_ops<u8, 8>();
    integer_ops<u8, 16>();
    integer_ops<u8, 32>();
    integer_ops<u8, 64>();
    integer_ops<i8, 16>();
    integer_ops<u16, 8>();
    integer_ops<i16, 8>();
    integer_ops<u32, 4>();
    integer_ops<i32, 4>();
    integer_ops<u64, 2>();
    integer_ops<i64, 2>();
    integer_ops<usize, 2>();
    integer_ops<isize, 2>();
    integer_ops<i64, 64>();
}

TEST(Simd, FloatingArithmeticAndMasks) {
    floats<f32>();
    floats<f64>();
    memory_and_masks<f64, 64>();
}

TEST(Simd, MemchrOffsetsAndFirstMatch) {
    search_cases();
}

TEST(Simd, GuardPageTailAccess) {
#if defined(__unix__)
    guard_pages();
#else
    GTEST_SKIP() << "guard pages require mmap";
#endif
}

TEST(Simd, RejectsShortInput) {
    EXPECT_DEATH(((void)Simd<u8, 16>::from_slice({})), "SIMD input slice too short");
}

TEST(Simd, RejectsShortOutput) {
    EXPECT_DEATH((Simd<u8, 16>().copy_to_slice({})), "SIMD output slice too short");
}

TEST(Simd, RejectsLaneIndex) {
    EXPECT_DEATH(((void)Simd<u8, 16>()[usize(16)]), "SIMD lane index out of bounds");
}

TEST(Simd, RejectsMaskIndex) {
    EXPECT_DEATH(((void)Mask<16>()[usize(16)]), "SIMD mask index out of bounds");
}

TEST(Simd, RejectsReplacementIndex) {
    EXPECT_DEATH(((void)Simd<u8, 16>().with_lane(usize(16), u8(0))),
                 "SIMD lane index out of bounds");
}

module;
#include <rstd/test/gtest.hpp>

export module rstd.core:simd_arch_tests;
import :arch.x86;
import :arch.arm;
import :simd;

using namespace rstd;
using simd::Simd;

auto same_float(f32 a, f32 b) -> bool {
    return (a.is_nan() && b.is_nan()) || a.to_bits() == b.to_bits();
}

template<typename V>
concept Estimates = requires(V value) {
    value.recip_estimate();
    value.rsqrt_estimate();
};
static_assert(Estimates<Simd<f32, 4>>);
static_assert(! Estimates<Simd<f64, 4>> && ! Estimates<Simd<i32, 4>>);

template<size_t N>
void reciprocal_estimates() {
    SCOPED_TRACE(__PRETTY_FUNCTION__);
    using V = Simd<f32, N>;
    const u32 cases[] { u32(0),          u32(0x80000000), u32(0x3f800000), u32(0x40800000),
                        u32(0x3eaaaaab), u32(0xbf800000), u32(0x7f800000), u32(0xff800000),
                        u32(0x7fc00001), u32(1),          u32(0x80000001), u32(0x00800000),
                        u32(0x7f7fffff) };
    for (size_t round = 0; round < 13; ++round) {
        array<f32, N> input {};
        for (size_t i = 0; i < N; ++i) input[usize(i)] = f32::from_bits(cases[(i + round) % 13]);
        auto value   = V::from_array(input);
        auto inverse = value.recip_estimate(), root = value.rsqrt_estimate();
        for (size_t i = 0; i < N; ++i) {
            auto x = input[usize(i)];
#if defined(__SSE__)
            auto scalar           = arch::x86::__m128 { x.to_primitive(), 0, 0, 0 };
            auto expected_inverse = f32(arch::x86::_mm_rcp_ss(scalar)[0]);
            auto expected_root    = f32(arch::x86::_mm_rsqrt_ss(scalar)[0]);
#elif defined(__ARM_NEON)
            auto scalar           = arch::arm::float32x2_t { x.to_primitive(), x.to_primitive() };
            auto expected_inverse = f32(arch::arm::vrecpe_f32(scalar)[0]);
            auto expected_root    = f32(arch::arm::vrsqrte_f32(scalar)[0]);
#else
            auto expected_inverse = f32(1) / x, expected_root = f32(1) / x.sqrt();
#endif
            ASSERT_TRUE(same_float(inverse[usize(i)], expected_inverse));
            ASSERT_TRUE(same_float(root[usize(i)], expected_root));
        }
    }
}

TEST(Simd, ReciprocalEstimates) {
    reciprocal_estimates<1>();
    reciprocal_estimates<2>();
    reciprocal_estimates<4>();
    reciprocal_estimates<8>();
    reciprocal_estimates<16>();
    reciprocal_estimates<32>();
    reciprocal_estimates<64>();
}

#if defined(__i386__) || defined(__x86_64__)
[[gnu::target("avx")]]
auto avx_header_estimates() -> bool {
    arch::x86::__m256 input { 1, 2, 3, 4, 8, 16, 32, 64 };
    auto              inverse = arch::x86::_mm256_rcp_ps(input);
    auto              root    = arch::x86::_mm256_rsqrt_ps(input);
    for (size_t i = 0; i < 8; ++i) {
        if (f32(inverse[i] * input[i] - 1).abs() > f32(0.001)) return false;
        if (f32(root[i] * root[i] * input[i] - 1).abs() > f32(0.002)) return false;
    }
    return true;
}

TEST(SimdArch, AvxHeaderIntrinsicTargetBoundary) {
    __builtin_cpu_init();
    if (! __builtin_cpu_supports("avx")) GTEST_SKIP() << "AVX unavailable";
    EXPECT_TRUE(avx_header_estimates());
}
#endif

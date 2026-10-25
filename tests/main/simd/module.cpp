#include <rstd/test/gtest.hpp>

import rstd;
import rstd.tests.simd_core_check;

using namespace rstd;

TEST(Simd, CoreAndStdImports) {
    EXPECT_TRUE(simd_core_check());
    EXPECT_TRUE((simd::Simd<i32, 4>::splat(i32(3)).reduce_sum() == i32(12)));
}

TEST(Simd, ReciprocalEstimatesAcrossModules) {
    EXPECT_TRUE(simd_estimates_core_check());
    using V    = simd::Simd<f32, 4>;
    auto input = V::splat(f32(4));
    hint::black_box(input);
    EXPECT_TRUE(
        (input.recip_estimate() - V::splat(f32(0.25))).abs().simd_lt(V::splat(f32(0.002))).all());
    EXPECT_TRUE(
        (input.rsqrt_estimate() - V::splat(f32(0.5))).abs().simd_lt(V::splat(f32(0.002))).all());
}

TEST(Simd, Avx2TargetBoundary) {
#if defined(__x86_64__)
    __builtin_cpu_init();
    if (! __builtin_cpu_supports("avx2")) {
        GTEST_SKIP() << "AVX2 unavailable";
    }
    f32 input[8] {};
    f32 output[8] {};
    for (int i = 0; i < 8; ++i) input[i] = f32(float(i + 1));
    hint::black_box(input);
    simd_avx_transform(input, output);
    for (int i = 0; i < 8; ++i) EXPECT_EQ(output[i], input[i] * f32(2));
#else
    GTEST_SKIP() << "AVX2 requires x86_64";
#endif
}

TEST(Simd, SeparateRoundingAndSignedZero) {
#if defined(__x86_64__)
    __builtin_cpu_init();
    if (! __builtin_cpu_supports("avx2") || ! __builtin_cpu_supports("fma")) {
        GTEST_SKIP() << "AVX2/FMA unavailable";
    }
    EXPECT_TRUE(simd_separate_rounding());
#else
    GTEST_SKIP() << "AVX2/FMA requires x86_64";
#endif
}

#include <rstd/test/gtest.hpp>

import rstd;
import rstd.tests.simd_core_check;

using namespace rstd;

TEST(Simd, CoreAndStdImports) {
    EXPECT_TRUE(simd_core_check());
    EXPECT_TRUE((simd::Simd<i32, 4>::splat(i32(3)).reduce_sum() == i32(12)));
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

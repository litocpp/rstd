export module rstd.tests.simd_core_check;

import rstd.core;
export auto simd_core_check() -> bool {
    return rstd::simd::Simd<rstd::i32, 4>::splat(rstd::i32(2)).reduce_sum() == rstd::i32(8);
}

#if defined(__x86_64__)
export [[gnu::target("avx2")]]
void simd_avx_transform(const rstd::f32* input, rstd::f32* output) {
    using namespace rstd;
    using V     = simd::Simd<f32, 8>;
    auto result = V::from_slice(slice<f32>::from_raw_parts(input, usize(8))) * V::splat(f32(2));
    result.copy_to_slice(mut_ref<f32[]>::from_raw_parts(output, usize(8)));
}

export [[gnu::target("avx2,fma")]]
auto simd_separate_rounding() -> bool {
    using namespace rstd;
    using V = simd::Simd<f32, 4>;
    auto a = V::splat(f32(1) + f32::EPSILON), b = V::splat(f32(1) - f32::EPSILON);
    hint::black_box(a);
    hint::black_box(b);
    auto separate = a * b + V::splat(f32(-1));
    auto fused    = a.mul_add(b, V::splat(f32(-1)));
    auto zeros    = (V::splat(f32(-0.0)) + V::splat(f32(-0.0))) * V::splat(f32(0.5));
    return separate == V::splat(f32(0)) && fused == V::splat(-(f32::EPSILON * f32::EPSILON)) &&
           zeros.is_sign_negative().all();
}
#endif

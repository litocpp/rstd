module;
#include <string.h>

module rstd_benches;
import :simd.kernels;
import rstd.core;

using namespace rstd;
using simd::Simd;

namespace rstd_bench::simd_kernels
{

[[gnu::noinline]]
auto search_scalar(slice<u8> bytes, u8 needle) -> usize {
    for (size_t i = 0; i < bytes.len().to_primitive(); ++i)
        if (bytes[usize(i)] == needle) return usize(i);
    return bytes.len();
}
[[gnu::noinline]]
auto search_simd(slice<u8> bytes, u8 needle) -> usize {
    auto found = memchr::memchr(needle, bytes);
    return found.is_some() ? found.unwrap_unchecked() : bytes.len();
}
[[gnu::noinline]]
auto search_libc(slice<u8> bytes, u8 needle) -> usize {
    if (bytes.is_empty()) return usize();
    const auto* start = bytes.as_raw_ptr();
    const auto* found = static_cast<const byte*>(
        ::memchr(start, needle.to_primitive(), bytes.len().to_primitive()));
    return found ? usize(found - start) : bytes.len();
}

[[gnu::noinline]]
void transform_slice(mut_ref<f32[]> output, slice<f32> input) {
    if (output.len() < input.len()) panic { "transform output too short" };
    for (usize i; i < input.len(); ++i) output[i] = input[i] * f32(2) + f32(1);
}

[[gnu::noinline]]
void transform_scalar(f32* __restrict out, const f32* __restrict in, size_t n) {
    for (size_t i = 0; i < n; ++i) out[i] = in[i] * f32(2) + f32(1);
}
[[gnu::noinline]]
void transform_native(f32* __restrict out, const f32* __restrict in, size_t n) {
    using Raw = float __attribute__((ext_vector_type(4)));
    size_t i  = 0;
    for (; n - i >= 4; i += 4) {
        Raw x {};
        for (size_t j = 0; j < 4; ++j) x[j] = in[i + j].to_primitive();
        x = x * Raw(2.0f) + Raw(1.0f);
        for (size_t j = 0; j < 4; ++j) out[i + j] = f32(x[j]);
    }
    for (; i < n; ++i) out[i] = in[i] * f32(2) + f32(1);
}
[[gnu::noinline]]
void transform_simd(f32* __restrict out, const f32* __restrict in, size_t n) {
    using V  = Simd<f32, 4>;
    size_t i = 0;
    for (; n - i >= 4; i += 4) {
        auto x = V::from_slice(slice<f32>::from_raw_parts(in + i, usize(4)));
        (x * V::splat(f32(2)) + V::splat(f32(1)))
            .copy_to_slice(mut_ref<f32[]>::from_raw_parts(out + i, usize(4)));
    }
    for (; i < n; ++i) out[i] = in[i] * f32(2) + f32(1);
}
[[gnu::noinline]]
auto sum_scalar(const f32* in, size_t n) -> f32 {
    float sum = 0;
    for (size_t i = 0; i < n; ++i) sum += in[i].to_primitive();
    return f32(sum);
}
[[gnu::noinline]]
auto sum_simd(const f32* in, size_t n) -> f32 {
    using V = Simd<f32, 4>;
    f32    sum;
    size_t i = 0;
    for (; n - i >= 4; i += 4)
        sum = V::from_slice(slice<f32>::from_raw_parts(in + i, usize(4))).reduce_sum_ordered(sum);
    for (; i < n; ++i) sum += in[i];
    return sum;
}

} // namespace rstd_bench::simd_kernels

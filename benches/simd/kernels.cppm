export module rstd_benches:simd.kernels;
import rstd.core;

export namespace rstd_bench::simd_kernels
{
auto search_scalar(rstd::slice<rstd::u8> bytes, rstd::u8 needle) -> rstd::usize;
auto search_simd(rstd::slice<rstd::u8> bytes, rstd::u8 needle) -> rstd::usize;
auto search_libc(rstd::slice<rstd::u8> bytes, rstd::u8 needle) -> rstd::usize;
void transform_slice(rstd::mut_ref<rstd::f32[]> output, rstd::slice<rstd::f32> input);
void transform_scalar(rstd::f32* output, const rstd::f32* input, rstd::size_t n);
void transform_native(rstd::f32* output, const rstd::f32* input, rstd::size_t n);
void transform_simd(rstd::f32* output, const rstd::f32* input, rstd::size_t n);
auto sum_scalar(const rstd::f32* input, rstd::size_t n) -> rstd::f32;
auto sum_simd(const rstd::f32* input, rstd::size_t n) -> rstd::f32;
} // namespace rstd_bench::simd_kernels

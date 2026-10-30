import rstd.core;

using namespace rstd::prelude;
using rstd::simd::Simd;

template<class F,rstd::size_t N>
auto square_roots() -> bool {
    using V=Simd<F,N>;array<F,N> squares{};
    for(rstd::size_t i{};i<N;++i)squares[usize(i)]=F((i+1)*(i+1));
    auto roots=V::from_array(squares).sqrt();
    for(rstd::size_t i{};i<N;++i)if(roots[usize(i)]!=F(i+1))return false;
    auto zero=V::splat(F(-0.0));if(zero.sqrt().to_bits()!=zero.to_bits())return false;
    if(V::splat(F::INFINITY_).sqrt()!=V::splat(F::INFINITY_))return false;
    if(!V::splat(F(-1)).sqrt().is_nan().all())return false;
    if(!V::splat(F::NAN_).sqrt().is_nan().all())return false;
    if(V::splat(F(2)).sqrt()!=V::splat(F(2).sqrt()))return false;
    return true;
}

template<class F>
auto widths() -> bool {
    return square_roots<F,1>() && square_roots<F,2>() && square_roots<F,4>() && square_roots<F,8>() &&
        square_roots<F,16>() && square_roots<F,32>() && square_roots<F,64>();
}

auto main() -> int {
    if(!widths<f32>() || !widths<f64>())return 1;
    auto single=Simd<f32,4>::splat(f32::from_bits(u32(1))).sqrt();
    if(single.to_bits()!=Simd<u32,4>::splat(u32(0x1a3504f3)))return 2;
    auto twice=Simd<f64,4>::splat(f64::from_bits(u64(1))).sqrt();
    if(twice.to_bits()!=Simd<u64,4>::splat(u64(0x1e60000000000000ull)))return 3;
    return 0;
}

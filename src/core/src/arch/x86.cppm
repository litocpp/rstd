module;
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
#include <immintrin.h>
#endif

export module rstd.core:arch.x86;

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
// Header-local functions are only imported within rstd.core, never re-exported.
namespace rstd::arch::x86
{
inline void spin_loop() {
    __builtin_ia32_pause();
}

using ::__m128;
using ::__m256;
using ::_mm_rcp_ps;
using ::_mm_rcp_ss;
using ::_mm_rsqrt_ps;
using ::_mm_rsqrt_ss;
using ::_mm256_rcp_ps;
using ::_mm256_rsqrt_ps;
#if defined(__SSE__)
template<auto N>
inline constexpr auto reciprocal_estimate_width =
#if defined(__AVX__)
    N >= 8 ? 8 : 4;
#else
    4;
#endif

template<bool SquareRoot, typename Vector>
auto reciprocal_estimate(Vector input) noexcept -> Vector {
    using Expected = float __attribute__((ext_vector_type(sizeof(Vector) / sizeof(float))));
    static_assert(__is_same(Vector, Expected));
#if defined(__AVX__)
    static_assert(sizeof(Vector) == 16 || sizeof(Vector) == 32);
    if constexpr (sizeof(Vector) == 32) {
        if constexpr (SquareRoot)
            return __builtin_ia32_rsqrtps256(input);
        else
            return __builtin_ia32_rcpps256(input);
    } else
#else
    static_assert(sizeof(Vector) == 16);
#endif
    {
        if constexpr (SquareRoot)
            return __builtin_ia32_rsqrtps(input);
        else
            return __builtin_ia32_rcpps(input);
    }
}
#endif
} // namespace rstd::arch::x86
#endif

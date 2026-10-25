export module rstd.core:arch;
export import :arch.x86;
export import :arch.arm;

namespace rstd::arch
{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
using x86::spin_loop;
#elif defined(__aarch64__) || defined(_M_ARM64)
using arm::spin_loop;
#else
inline void spin_loop() {
}
#endif

// Select from the module's compilation target, not the caller's target attribute.
#if defined(__SSE__)
using x86::reciprocal_estimate_width;
using x86::reciprocal_estimate;
#elif defined(__ARM_NEON)
using arm::reciprocal_estimate_width;
using arm::reciprocal_estimate;
#else
template<auto N>
inline constexpr auto reciprocal_estimate_width = N;

template<bool SquareRoot, typename Vector>
auto reciprocal_estimate(Vector input) noexcept -> Vector {
    using Expected = float __attribute__((ext_vector_type(sizeof(Vector) / sizeof(float))));
    static_assert(__is_same(Vector, Expected));
    Vector result;
    for (decltype(sizeof(Vector)) i = 0; i < sizeof(Vector) / sizeof(float); ++i) {
        if constexpr (SquareRoot)
            result[i] = 1.0f / static_cast<float>(__builtin_sqrt(input[i]));
        else
            result[i] = 1.0f / input[i];
    }
    return result;
}
#endif
} // namespace rstd::arch

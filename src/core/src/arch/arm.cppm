module;
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

export module rstd.core:arch.arm;

namespace rstd::arch::arm
{
#if defined(__aarch64__) || defined(_M_ARM64)
inline void spin_loop() {
    __builtin_arm_isb(15);
}
#endif

#if defined(__ARM_NEON)
// NEON types and functions shared by ARM and AArch64.
using ::float32x2_t;
using ::float32x4_t;
using ::int8x8_t;
using ::int8x16_t;
using ::vrecpe_f32;
using ::vrecpeq_f32;
using ::vrsqrte_f32;
using ::vrsqrteq_f32;
template<auto N>
inline constexpr auto reciprocal_estimate_width = N >= 4 ? 4 : 2;

template<bool SquareRoot, typename Vector>
auto reciprocal_estimate(Vector input) noexcept -> Vector {
    using Expected = float __attribute__((ext_vector_type(sizeof(Vector) / sizeof(float))));
    static_assert(__is_same(Vector, Expected));
    static_assert(sizeof(Vector) == 8 || sizeof(Vector) == 16);
    // Clang NEON overload codes: float32x4_t = 41, float32x2_t = 9.
    if constexpr (sizeof(Vector) == 16) {
        auto packed = __builtin_bit_cast(int8x16_t, input);
        if constexpr (SquareRoot)
            return __builtin_bit_cast(Vector, __builtin_neon_vrsqrteq_v(packed, 41));
        else
            return __builtin_bit_cast(Vector, __builtin_neon_vrecpeq_v(packed, 41));
    } else {
        auto packed = __builtin_bit_cast(int8x8_t, input);
        if constexpr (SquareRoot)
            return __builtin_bit_cast(Vector, __builtin_neon_vrsqrte_v(packed, 9));
        else
            return __builtin_bit_cast(Vector, __builtin_neon_vrecpe_v(packed, 9));
    }
}
#endif
} // namespace rstd::arch::arm

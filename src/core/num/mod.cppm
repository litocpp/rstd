module;
#include <rstd/macro.hpp>
export module rstd.core:num;
export import :num.nonzero;
export import :num.niche_types;
export import :num.integer;

namespace rstd::num
{

/**
 * @brief Counts the number of consecutive zero bits, starting from the most significant bit.
 * @param x An unsigned integer.
 * @return The number of leading zero bits.
 */
export template<typename T>
constexpr int countl_zero(T x) noexcept {
    constexpr int digits = rstd::numeric_limits<T>::digits;
    if (x == 0) return digits;

    constexpr int uint_bits      = rstd::numeric_limits<unsigned int>::digits;
    constexpr int ulong_bits     = rstd::numeric_limits<unsigned long>::digits;
    constexpr int ulonglong_bits = rstd::numeric_limits<unsigned long long>::digits;

    // Dispatch to the appropriate compiler builtin based on type size
    if constexpr (digits <= uint_bits) {
        return __builtin_clz(static_cast<unsigned int>(x)) - (uint_bits - digits);
    } else if constexpr (digits <= ulong_bits) {
        return __builtin_clzl(static_cast<unsigned long>(x)) - (ulong_bits - digits);
    } else if constexpr (digits <= ulonglong_bits) {
        return __builtin_clzll(static_cast<unsigned long long>(x)) - (ulonglong_bits - digits);
    } else {
        // Handle 128-bit integers by splitting into high/low 64-bit parts
        unsigned long long high = static_cast<unsigned long long>(x >> ulonglong_bits);
        if (high != 0) {
            constexpr int diff = ulonglong_bits - (digits - ulonglong_bits);
            return __builtin_clzll(high) - diff;
        }
        unsigned long long low = static_cast<unsigned long long>(x);
        return (digits - ulonglong_bits) + __builtin_clzll(low);
    }
}

/**
 * @brief Returns the smallest power of two greater than or equal to x.
 * @param x An unsigned integer.
 * @return The smallest power of two >= x.
 * @note If the result is not representable in T, the behavior is undefined.
 */
export template<typename T>
constexpr T bit_ceil(T x) noexcept {
    if (x <= 1u) {
        return T(1);
    }
    constexpr int width = rstd::numeric_limits<T>::digits;

    int shift_exponent = width - countl_zero(static_cast<T>(x - 1u));

    assert(shift_exponent < width);
    return T(1) << shift_exponent;
}

} // namespace rstd::num
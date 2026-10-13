module;
#include <rstd/macro.hpp>

module rstd.runtime;

#if RSTD_ARCH_WASM && RSTD_OS_UNKNOWN

template<typename Float>
auto wasm_ldexp(Float value, int exponent) noexcept -> Float {
    using Bits                = rstd::mtp::cond<sizeof(Float) == 4, rstd::uint32_t, rstd::uint64_t>;
    constexpr int  fraction   = sizeof(Float) == 4 ? 23 : 52;
    constexpr int  maximum    = sizeof(Float) == 4 ? 127 : 1023;
    constexpr int  minimum    = 1 - maximum;
    constexpr int  down       = -minimum - (fraction + 1);
    constexpr auto up_scale   = __builtin_bit_cast(Float, Bits(2 * maximum) << fraction);
    constexpr auto down_scale = __builtin_bit_cast(Float, Bits(fraction + 2) << fraction);

    if (exponent > maximum) {
        value *= up_scale;
        exponent -= maximum;
        if (exponent > maximum) {
            value *= up_scale;
            exponent -= maximum;
            if (exponent > maximum) exponent = maximum;
        }
    } else if (exponent < minimum) {
        value *= down_scale;
        exponent += down;
        if (exponent < minimum) {
            value *= down_scale;
            exponent += down;
            if (exponent < minimum) exponent = minimum;
        }
    }
    const auto scale = __builtin_bit_cast(Float, Bits(exponent + maximum) << fraction);
    return value * scale;
}

extern "C" float ldexpf(float value, int exponent) noexcept {
    return wasm_ldexp(value, exponent);
}

extern "C" double ldexp(double value, int exponent) noexcept {
    return wasm_ldexp(value, exponent);
}

#endif

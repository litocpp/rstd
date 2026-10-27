#include <stdint.h>
#include "platform.h"

#if RSTD_RUNTIME_WASM

#define RSTD_LDEXP(name, Float, Bits, fraction, maximum)                                       \
    Float name(Float value, int exponent) {                                                    \
        const int   minimum    = 1 - maximum;                                                  \
        const int   down       = -minimum - (fraction + 1);                                    \
        const Float up_scale   = __builtin_bit_cast(Float, (Bits)(2 * maximum) << fraction);   \
        const Float down_scale = __builtin_bit_cast(Float, (Bits)(fraction + 2) << fraction);  \
        if (exponent > maximum) {                                                              \
            value *= up_scale;                                                                 \
            exponent -= maximum;                                                               \
            if (exponent > maximum) {                                                          \
                value *= up_scale;                                                             \
                exponent -= maximum;                                                           \
                if (exponent > maximum) exponent = maximum;                                    \
            }                                                                                  \
        } else if (exponent < minimum) {                                                       \
            value *= down_scale;                                                               \
            exponent += down;                                                                  \
            if (exponent < minimum) {                                                          \
                value *= down_scale;                                                           \
                exponent += down;                                                              \
                if (exponent < minimum) exponent = minimum;                                    \
            }                                                                                  \
        }                                                                                      \
        const Float scale = __builtin_bit_cast(Float, (Bits)(exponent + maximum) << fraction); \
        return value * scale;                                                                  \
    }

RSTD_LDEXP(ldexpf, float, uint32_t, 23, 127)
RSTD_LDEXP(ldexp, double, uint64_t, 52, 1023)

#undef RSTD_LDEXP

#endif

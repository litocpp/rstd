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

#if defined(_WIN32)

// Windows CRT lacks the roundeven entry points emitted by Clang.
#define RSTD_ROUNDEVEN(name, Float, Bits, fraction, bias)                                   \
    Float name(Float value) {                                                               \
        Bits           bits      = __builtin_bit_cast(Bits, value);                         \
        const Bits     sign_mask = (Bits)1 << (sizeof(Bits) * 8 - 1);                       \
        const Bits     sign      = bits & sign_mask;                                        \
        const Bits     magnitude = bits & ~sign_mask;                                       \
        const unsigned exponent  = (unsigned)(magnitude >> fraction);                       \
        if (exponent >= bias + fraction) {                                                  \
            return exponent == 2 * bias + 1 ? value + value : value;                        \
        }                                                                                   \
        if (exponent < bias) {                                                              \
            bits = sign;                                                                    \
            if (magnitude > ((Bits)(bias - 1) << fraction)) bits |= (Bits)bias << fraction; \
            return __builtin_bit_cast(Float, bits);                                         \
        }                                                                                   \
        const unsigned shift = bias + fraction - exponent;                                  \
        const Bits     unit  = (Bits)1 << shift;                                            \
        const Bits     mask  = unit - 1;                                                    \
        const Bits     tail  = bits & mask;                                                 \
        bits &= ~mask;                                                                      \
        if (tail > (unit >> 1) || (tail == (unit >> 1) && (bits & unit))) bits += unit;     \
        return __builtin_bit_cast(Float, bits);                                             \
    }

RSTD_ROUNDEVEN(roundevenf, float, uint32_t, 23, 127)
RSTD_ROUNDEVEN(roundeven, double, uint64_t, 52, 1023)

#undef RSTD_ROUNDEVEN

#endif

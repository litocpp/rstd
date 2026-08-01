export module rstd.core:num.flt2dec;
import :num.types;
import :array;
import :num.bignum;
import rstd.basic;

namespace rstd::num::flt2dec
{

export template<typename T>
concept Float = mtp::any<T, f32, f64>;

export enum class Category : rstd::uint8_t { Nan, Infinite, Zero, Finite };

export constexpr rstd::size_t MAX_SIG_DIGITS = 17;
export constexpr rstd::size_t DIGIT_CAPACITY = 1024;

export template<rstd::size_t Capacity>
struct Decimal {
    Category                       category = Category::Zero;
    bool                           negative = false;
    array<rstd::uint8_t, Capacity> digits {};
    rstd::size_t                   len      = 0;
    rstd::int16_t                  exponent = 0;
};

export using ShortestDecimal = Decimal<MAX_SIG_DIGITS>;
export using ExactDecimal    = Decimal<DIGIT_CAPACITY>;

} // namespace rstd::num::flt2dec

using namespace rstd::prelude;
using namespace rstd::num::flt2dec;

struct Decoded {
    rstd::uint64_t mant;
    rstd::uint64_t minus;
    rstd::uint64_t plus;
    rstd::int16_t  exponent;
    bool           inclusive;
};

struct FullDecoded {
    Category category;
    bool     negative;
    Decoded  finite;
};

template<Float T>
struct FloatLayout;

template<>
struct FloatLayout<f32> {
    using Bits = rstd::uint32_t;

    static constexpr rstd::size_t  MANTISSA_BITS = 23;
    static constexpr rstd::size_t  EXPONENT_BITS = 8;
    static constexpr rstd::int16_t EXPONENT_BIAS = 127;
};

template<>
struct FloatLayout<f64> {
    using Bits = rstd::uint64_t;

    static constexpr rstd::size_t  MANTISSA_BITS = 52;
    static constexpr rstd::size_t  EXPONENT_BITS = 11;
    static constexpr rstd::int16_t EXPONENT_BIAS = 1023;
};

template<Float T>
constexpr auto decode(T value) noexcept -> FullDecoded {
    using Layout = FloatLayout<T>;
    using Bits   = typename Layout::Bits;

    constexpr Bits MANTISSA_MASK = (Bits(1) << Layout::MANTISSA_BITS) - 1;
    constexpr Bits EXPONENT_MASK = (Bits(1) << Layout::EXPONENT_BITS) - 1;

    const Bits bits        = rstd::bit_cast<Bits>(value);
    const bool negative    = (bits >> (Layout::MANTISSA_BITS + Layout::EXPONENT_BITS)) != 0;
    const Bits raw_mant    = bits & MANTISSA_MASK;
    const Bits raw_exp     = (bits >> Layout::MANTISSA_BITS) & EXPONENT_MASK;
    const auto empty_value = Decoded {};

    if (raw_exp == EXPONENT_MASK) {
        return { raw_mant == 0 ? Category::Infinite : Category::Nan, negative, empty_value };
    }
    if (raw_exp == 0 && raw_mant == 0) {
        return { Category::Zero, negative, empty_value };
    }

    if (raw_exp == 0) {
        const rstd::uint64_t mant = static_cast<rstd::uint64_t>(raw_mant) << 1;
        const rstd::int16_t  exp  = static_cast<rstd::int16_t>(
            -Layout::EXPONENT_BIAS - static_cast<rstd::int16_t>(Layout::MANTISSA_BITS));
        return { Category::Finite, negative, { mant, 1, 1, exp, true } };
    }

    const rstd::uint64_t mant =
        static_cast<rstd::uint64_t>((Bits(1) << Layout::MANTISSA_BITS) | raw_mant);
    const rstd::int16_t exp = static_cast<rstd::int16_t>(raw_exp) - Layout::EXPONENT_BIAS -
                              static_cast<rstd::int16_t>(Layout::MANTISSA_BITS);
    if (raw_mant == 0) {
        return { Category::Finite,
                 negative,
                 { mant << 2, 1, 2, static_cast<rstd::int16_t>(exp - 2), true } };
    }
    return { Category::Finite,
             negative,
             { mant << 1, 1, 1, static_cast<rstd::int16_t>(exp - 1), (mant & 1) == 0 } };
}

using FloatBig = rstd::num::bignum::FixedBig<40>;

constexpr rstd::uint32_t POW10[] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
};
constexpr rstd::uint32_t POW5_TO_16[]  = { 0x86f26fc1, 0x23 };
constexpr rstd::uint32_t POW5_TO_32[]  = { 0x85acef81, 0x2d6d415b, 0x4ee };
constexpr rstd::uint32_t POW5_TO_64[]  = { 0xbf6a1f01,
                                           0x6e38ed64,
                                           0xdaa797ed,
                                           0xe93ff9f4,
                                           0x184f03 };
constexpr rstd::uint32_t POW5_TO_128[] = {
    0x2e953e01, 0x3df9909,  0xf1538fd,  0x2374e42f, 0xd3cff5ec,
    0xc404dc08, 0xbccdb0da, 0xa6337f19, 0xe91f2603, 0x24e,
};
constexpr rstd::uint32_t POW5_TO_256[] = {
    0x982e7c01, 0xbed3875b, 0xd8d99f72, 0x12152f87, 0x6bde50c6, 0xcf4a6e70, 0xd595d80f,
    0x26b2716e, 0xadc666b0, 0x1d153624, 0x3c42d35a, 0x63ff540e, 0xcc5573c0, 0x65f9ef17,
    0x55bc28f2, 0x80dcc7f7, 0xf46eeddc, 0x5fdcefce, 0x553f7,
};

template<rstd::size_t N>
void mul_if(FloatBig&    value,
            rstd::size_t exponent,
            rstd::size_t bit,
            const rstd::uint32_t (&digits)[N]) {
    if ((exponent & bit) != 0) value.mul_digits(digits, N);
}

void mul_pow10(FloatBig& value, rstd::size_t exponent) {
    if (exponent < 8) {
        value.mul_small(POW10[exponent]);
        return;
    }
    if ((exponent & 7) != 0) value.mul_small(POW10[exponent & 7] >> (exponent & 7));
    if ((exponent & 8) != 0) value.mul_small(POW10[8] >> 8);
    mul_if(value, exponent, 16, POW5_TO_16);
    mul_if(value, exponent, 32, POW5_TO_32);
    mul_if(value, exponent, 64, POW5_TO_64);
    mul_if(value, exponent, 128, POW5_TO_128);
    mul_if(value, exponent, 256, POW5_TO_256);
    value.mul_pow2(exponent);
}

void div_2pow10(FloatBig& value, rstd::size_t exponent) {
    constexpr rstd::size_t LARGEST = sizeof(POW10) / sizeof(POW10[0]) - 1;
    while (exponent > LARGEST) {
        value.div_rem_small(POW10[LARGEST]);
        exponent -= LARGEST;
    }
    value.div_rem_small(POW10[exponent] << 1);
}

auto estimate_scaling_factor(rstd::uint64_t mant, rstd::int16_t exponent) noexcept
    -> rstd::int16_t {
    const rstd::int64_t nbits = 64 - static_cast<rstd::int64_t>(__builtin_clzll(mant - 1));
    return static_cast<rstd::int16_t>(((nbits + exponent) * 1292913986) >> 32);
}

auto round_up(rstd::uint8_t* digits, rstd::size_t len) noexcept -> rstd::uint8_t {
    for (rstd::size_t i = len; i != 0; --i) {
        if (digits[i - 1] != '9') {
            ++digits[i - 1];
            for (rstd::size_t j = i; j < len; ++j) digits[j] = '0';
            return 0;
        }
    }
    if (len != 0) {
        digits[0] = '1';
        for (rstd::size_t i = 1; i < len; ++i) digits[i] = '0';
    }
    return len == 0 ? '1' : '0';
}

auto digit(FloatBig&       value,
           FloatBig const& scale,
           FloatBig const& scale2,
           FloatBig const& scale4,
           FloatBig const& scale8) -> rstd::uint8_t {
    rstd::uint8_t result = 0;
    if (value.compare(scale8) >= 0) {
        value.sub(scale8);
        result += 8;
    }
    if (value.compare(scale4) >= 0) {
        value.sub(scale4);
        result += 4;
    }
    if (value.compare(scale2) >= 0) {
        value.sub(scale2);
        result += 2;
    }
    if (value.compare(scale) >= 0) {
        value.sub(scale);
        ++result;
    }
    return result;
}

struct Digits {
    rstd::size_t  len;
    rstd::int16_t exponent;
};

auto format_shortest(Decoded const& decoded, rstd::uint8_t* buffer) -> Digits {
    rstd::int16_t k = estimate_scaling_factor(decoded.mant + decoded.plus, decoded.exponent);

    FloatBig mant  = FloatBig::from_u64(decoded.mant);
    FloatBig minus = FloatBig::from_u64(decoded.minus);
    FloatBig plus  = FloatBig::from_u64(decoded.plus);
    FloatBig scale = FloatBig::from_small(1);
    if (decoded.exponent < 0) {
        scale.mul_pow2(static_cast<rstd::size_t>(-decoded.exponent));
    } else {
        mant.mul_pow2(static_cast<rstd::size_t>(decoded.exponent));
        minus.mul_pow2(static_cast<rstd::size_t>(decoded.exponent));
        plus.mul_pow2(static_cast<rstd::size_t>(decoded.exponent));
    }

    if (k >= 0) {
        mul_pow10(scale, static_cast<rstd::size_t>(k));
    } else {
        mul_pow10(mant, static_cast<rstd::size_t>(-k));
        mul_pow10(minus, static_cast<rstd::size_t>(-k));
        mul_pow10(plus, static_cast<rstd::size_t>(-k));
    }

    FloatBig upper = mant;
    upper.add(plus);
    const bool high_reaches_scale =
        decoded.inclusive ? scale.compare(upper) <= 0 : scale.compare(upper) < 0;
    if (high_reaches_scale) {
        ++k;
    } else {
        mant.mul_small(10);
        minus.mul_small(10);
        plus.mul_small(10);
    }

    FloatBig scale2 = scale;
    FloatBig scale4 = scale;
    FloatBig scale8 = scale;
    scale2.mul_pow2(1);
    scale4.mul_pow2(2);
    scale8.mul_pow2(3);

    rstd::size_t len = 0;
    bool         down;
    bool         up;
    for (;;) {
        buffer[len++] =
            static_cast<rstd::uint8_t>('0' + digit(mant, scale, scale2, scale4, scale8));

        down  = decoded.inclusive ? mant.compare(minus) <= 0 : mant.compare(minus) < 0;
        upper = mant;
        upper.add(plus);
        up = decoded.inclusive ? scale.compare(upper) <= 0 : scale.compare(upper) < 0;
        if (down || up) break;

        mant.mul_small(10);
        minus.mul_small(10);
        plus.mul_small(10);
    }

    FloatBig twice_mant = mant;
    twice_mant.mul_pow2(1);
    if (up && (! down || twice_mant.compare(scale) >= 0)) {
        if (const rstd::uint8_t carry = round_up(buffer, len); carry != 0) {
            buffer[len++] = carry;
            ++k;
        }
    }
    return { len, k };
}

auto format_exact(Decoded const& decoded,
                  rstd::uint8_t* buffer,
                  rstd::size_t   capacity,
                  rstd::int16_t  limit) -> Digits {
    rstd::int16_t k = estimate_scaling_factor(decoded.mant, decoded.exponent);

    FloatBig mant  = FloatBig::from_u64(decoded.mant);
    FloatBig scale = FloatBig::from_small(1);
    if (decoded.exponent < 0) {
        scale.mul_pow2(static_cast<rstd::size_t>(-decoded.exponent));
    } else {
        mant.mul_pow2(static_cast<rstd::size_t>(decoded.exponent));
    }

    if (k >= 0) {
        mul_pow10(scale, static_cast<rstd::size_t>(k));
    } else {
        mul_pow10(mant, static_cast<rstd::size_t>(-k));
    }

    FloatBig half_ulp = scale;
    div_2pow10(half_ulp, capacity);
    half_ulp.add(mant);
    if (half_ulp.compare(scale) >= 0) {
        ++k;
    } else {
        mant.mul_small(10);
    }

    rstd::size_t len;
    if (k < limit) {
        len = 0;
    } else {
        const rstd::int32_t requested = static_cast<rstd::int32_t>(k) - limit;
        len = static_cast<rstd::size_t>(requested) < capacity ? static_cast<rstd::size_t>(requested)
                                                              : capacity;
    }

    if (len != 0) {
        FloatBig scale2 = scale;
        FloatBig scale4 = scale;
        FloatBig scale8 = scale;
        scale2.mul_pow2(1);
        scale4.mul_pow2(2);
        scale8.mul_pow2(3);

        for (rstd::size_t i = 0; i < len; ++i) {
            if (mant.is_zero()) {
                for (; i < len; ++i) buffer[i] = '0';
                return { len, k };
            }
            buffer[i] =
                static_cast<rstd::uint8_t>('0' + digit(mant, scale, scale2, scale4, scale8));
            mant.mul_small(10);
        }
    }

    FloatBig five_scale = scale;
    five_scale.mul_small(5);
    const rstd::int8_t order = mant.compare(five_scale);
    if (order > 0 || (order == 0 && len != 0 && (buffer[len - 1] & 1) != 0)) {
        if (const rstd::uint8_t carry = round_up(buffer, len); carry != 0) {
            ++k;
            if (k > limit && len < capacity) buffer[len++] = carry;
        }
    }
    return { len, k };
}

auto max_buffer_len(rstd::int16_t exponent) noexcept -> rstd::size_t {
    const rstd::int32_t factor = exponent < 0 ? -12 : 5;
    return 21 + static_cast<rstd::size_t>(factor * static_cast<rstd::int32_t>(exponent)) / 16;
}

template<rstd::size_t Capacity>
auto make_decimal(FullDecoded const& decoded) -> Decimal<Capacity> {
    Decimal<Capacity> result {};
    result.category = decoded.category;
    result.negative = decoded.negative;
    return result;
}

namespace rstd::num::flt2dec
{

export template<Float T>
auto shortest(T value) -> ShortestDecimal {
    const auto decoded = decode(value);
    auto       result  = make_decimal<MAX_SIG_DIGITS>(decoded);
    if (decoded.category == Category::Finite) {
        const auto digits = format_shortest(decoded.finite, result.digits.data());
        result.len        = digits.len;
        result.exponent   = digits.exponent;
    }
    return result;
}

export template<Float T>
auto exact_fixed(T value, rstd::size_t fractional_digits) -> ExactDecimal {
    const auto decoded = decode(value);
    auto       result  = make_decimal<DIGIT_CAPACITY>(decoded);
    if (decoded.category == Category::Finite) {
        const rstd::size_t  capacity = max_buffer_len(decoded.finite.exponent);
        const rstd::int16_t limit =
            fractional_digits < 0x8000
                ? static_cast<rstd::int16_t>(-static_cast<rstd::int16_t>(fractional_digits))
                : ::raw_integer_min<rstd::int16_t>();
        const auto digits = format_exact(decoded.finite, result.digits.data(), capacity, limit);
        result.len        = digits.len;
        result.exponent   = digits.exponent;
    }
    return result;
}

export template<Float T>
auto exact_significant(T value, rstd::size_t significant_digits) -> ExactDecimal {
    const auto decoded = decode(value);
    auto       result  = make_decimal<DIGIT_CAPACITY>(decoded);
    if (decoded.category == Category::Finite) {
        const rstd::size_t max_len  = max_buffer_len(decoded.finite.exponent);
        const rstd::size_t capacity = significant_digits < max_len ? significant_digits : max_len;
        const auto         digits   = format_exact(
            decoded.finite, result.digits.data(), capacity, ::raw_integer_min<rstd::int16_t>());
        result.len      = digits.len;
        result.exponent = digits.exponent;
    }
    return result;
}

} // namespace rstd::num::flt2dec

export module rstd.core:num.flt2dec;
import :array;
import rstd.basic;

namespace rstd::num::flt2dec
{

export template<typename T>
concept Float = mtp::any<T, f32, f64>;

export enum class Category : u8 { Nan, Infinite, Zero, Finite };

export constexpr usize MAX_SIG_DIGITS = 17;
export constexpr usize DIGIT_CAPACITY = 1024;

export template<usize Capacity>
struct Decimal {
    Category            category = Category::Zero;
    bool                negative = false;
    array<u8, Capacity> digits {};
    usize               len      = 0;
    i16                 exponent = 0;
};

export using ShortestDecimal = Decimal<MAX_SIG_DIGITS>;
export using ExactDecimal    = Decimal<DIGIT_CAPACITY>;

namespace detail
{

struct Decoded {
    u64  mant;
    u64  minus;
    u64  plus;
    i16  exponent;
    bool inclusive;
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
    using Bits = u32;

    static constexpr usize MANTISSA_BITS = 23;
    static constexpr usize EXPONENT_BITS = 8;
    static constexpr i16   EXPONENT_BIAS = 127;
};

template<>
struct FloatLayout<f64> {
    using Bits = u64;

    static constexpr usize MANTISSA_BITS = 52;
    static constexpr usize EXPONENT_BITS = 11;
    static constexpr i16   EXPONENT_BIAS = 1023;
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
        const u64 mant = static_cast<u64>(raw_mant) << 1;
        const i16 exp =
            static_cast<i16>(-Layout::EXPONENT_BIAS - static_cast<i16>(Layout::MANTISSA_BITS));
        return { Category::Finite, negative, { mant, 1, 1, exp, true } };
    }

    const u64 mant = static_cast<u64>((Bits(1) << Layout::MANTISSA_BITS) | raw_mant);
    const i16 exp =
        static_cast<i16>(raw_exp) - Layout::EXPONENT_BIAS - static_cast<i16>(Layout::MANTISSA_BITS);
    if (raw_mant == 0) {
        return { Category::Finite, negative, { mant << 2, 1, 2, static_cast<i16>(exp - 2), true } };
    }
    return { Category::Finite,
             negative,
             { mant << 1, 1, 1, static_cast<i16>(exp - 1), (mant & 1) == 0 } };
}

class Big {
    static constexpr usize CAPACITY = 40;

    usize                size_ = 0;
    array<u32, CAPACITY> base_ {};

    static void capacity_error() { __builtin_trap(); }

public:
    static auto from_small(u32 value) noexcept -> Big {
        Big result;
        result.base_[0] = value;
        result.size_    = 1;
        return result;
    }

    static auto from_u64(u64 value) noexcept -> Big {
        Big result;
        while (value != 0) {
            result.base_[result.size_++] = static_cast<u32>(value);
            value >>= 32;
        }
        return result;
    }

    auto compare(Big const& other) const noexcept -> i8 {
        const usize size = size_ > other.size_ ? size_ : other.size_;
        for (usize i = size; i != 0; --i) {
            if (base_[i - 1] < other.base_[i - 1]) return -1;
            if (base_[i - 1] > other.base_[i - 1]) return 1;
        }
        return 0;
    }

    auto is_zero() const noexcept -> bool {
        for (usize i = 0; i < size_; ++i) {
            if (base_[i] != 0) return false;
        }
        return true;
    }

    void add(Big const& other) {
        usize size  = size_ > other.size_ ? size_ : other.size_;
        u64   carry = 0;
        for (usize i = 0; i < size; ++i) {
            const u64 value = static_cast<u64>(base_[i]) + other.base_[i] + carry;
            base_[i]        = static_cast<u32>(value);
            carry           = value >> 32;
        }
        if (carry != 0) {
            if (size == CAPACITY) capacity_error();
            base_[size++] = 1;
        }
        size_ = size;
    }

    void sub(Big const& other) {
        const usize size   = size_ > other.size_ ? size_ : other.size_;
        u64         borrow = 0;
        for (usize i = 0; i < size; ++i) {
            const u64 lhs = base_[i];
            const u64 rhs = static_cast<u64>(other.base_[i]) + borrow;
            base_[i]      = static_cast<u32>(lhs - rhs);
            borrow        = lhs < rhs;
        }
        if (borrow != 0) capacity_error();
        size_ = size;
    }

    void mul_small(u32 other) {
        usize size  = size_;
        u64   carry = 0;
        for (usize i = 0; i < size; ++i) {
            const u64 value = static_cast<u64>(base_[i]) * other + carry;
            base_[i]        = static_cast<u32>(value);
            carry           = value >> 32;
        }
        if (carry != 0) {
            if (size == CAPACITY) capacity_error();
            base_[size++] = static_cast<u32>(carry);
        }
        size_ = size;
    }

    void mul_pow2(usize bits) {
        const usize digits    = bits / 32;
        const usize shift     = bits % 32;
        const usize old_size  = size_;
        usize       next_size = old_size + digits;
        if (next_size > CAPACITY) capacity_error();

        for (usize i = old_size; i != 0; --i) base_[i - 1 + digits] = base_[i - 1];
        for (usize i = 0; i < digits; ++i) base_[i] = 0;

        if (shift != 0) {
            const u32 overflow = base_[next_size - 1] >> (32 - shift);
            if (overflow != 0) {
                if (next_size == CAPACITY) capacity_error();
                base_[next_size++] = overflow;
            }
            for (usize i = next_size - (overflow != 0); i > digits + 1; --i) {
                const usize index = i - 1;
                base_[index]      = (base_[index] << shift) | (base_[index - 1] >> (32 - shift));
            }
            base_[digits] <<= shift;
        }
        size_ = next_size;
    }

    void mul_digits(const u32* other, usize other_len) {
        array<u32, CAPACITY> result {};
        usize                result_size = 0;
        for (usize i = 0; i < size_; ++i) {
            if (base_[i] == 0) continue;
            u64 carry = 0;
            for (usize j = 0; j < other_len; ++j) {
                if (i + j >= CAPACITY) capacity_error();
                const u64 value = static_cast<u64>(base_[i]) * other[j] + result[i + j] + carry;
                result[i + j]   = static_cast<u32>(value);
                carry           = value >> 32;
            }
            usize size = other_len;
            if (carry != 0) {
                if (i + size >= CAPACITY) capacity_error();
                result[i + size++] = static_cast<u32>(carry);
            }
            if (result_size < i + size) result_size = i + size;
        }
        base_ = result;
        size_ = result_size;
    }

    auto div_rem_small(u32 other) -> u32 {
        u64 remainder = 0;
        for (usize i = size_; i != 0; --i) {
            const u64 value = (remainder << 32) | base_[i - 1];
            base_[i - 1]    = static_cast<u32>(value / other);
            remainder       = value % other;
        }
        return static_cast<u32>(remainder);
    }
};

constexpr u32 POW10[] = {
    1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000,
};
constexpr u32 POW5_TO_16[]  = { 0x86f26fc1, 0x23 };
constexpr u32 POW5_TO_32[]  = { 0x85acef81, 0x2d6d415b, 0x4ee };
constexpr u32 POW5_TO_64[]  = { 0xbf6a1f01, 0x6e38ed64, 0xdaa797ed, 0xe93ff9f4, 0x184f03 };
constexpr u32 POW5_TO_128[] = {
    0x2e953e01, 0x3df9909,  0xf1538fd,  0x2374e42f, 0xd3cff5ec,
    0xc404dc08, 0xbccdb0da, 0xa6337f19, 0xe91f2603, 0x24e,
};
constexpr u32 POW5_TO_256[] = {
    0x982e7c01, 0xbed3875b, 0xd8d99f72, 0x12152f87, 0x6bde50c6, 0xcf4a6e70, 0xd595d80f,
    0x26b2716e, 0xadc666b0, 0x1d153624, 0x3c42d35a, 0x63ff540e, 0xcc5573c0, 0x65f9ef17,
    0x55bc28f2, 0x80dcc7f7, 0xf46eeddc, 0x5fdcefce, 0x553f7,
};

template<usize N>
void mul_if(Big& value, usize exponent, usize bit, const u32 (&digits)[N]) {
    if ((exponent & bit) != 0) value.mul_digits(digits, N);
}

void mul_pow10(Big& value, usize exponent) {
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

void div_2pow10(Big& value, usize exponent) {
    constexpr usize LARGEST = sizeof(POW10) / sizeof(POW10[0]) - 1;
    while (exponent > LARGEST) {
        value.div_rem_small(POW10[LARGEST]);
        exponent -= LARGEST;
    }
    value.div_rem_small(POW10[exponent] << 1);
}

auto estimate_scaling_factor(u64 mant, i16 exponent) noexcept -> i16 {
    const i64 nbits = 64 - static_cast<i64>(__builtin_clzll(mant - 1));
    return static_cast<i16>(((nbits + exponent) * 1292913986) >> 32);
}

auto round_up(u8* digits, usize len) noexcept -> u8 {
    for (usize i = len; i != 0; --i) {
        if (digits[i - 1] != '9') {
            ++digits[i - 1];
            for (usize j = i; j < len; ++j) digits[j] = '0';
            return 0;
        }
    }
    if (len != 0) {
        digits[0] = '1';
        for (usize i = 1; i < len; ++i) digits[i] = '0';
    }
    return len == 0 ? '1' : '0';
}

auto digit(Big& value, Big const& scale, Big const& scale2, Big const& scale4, Big const& scale8)
    -> u8 {
    u8 result = 0;
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
    usize len;
    i16   exponent;
};

auto format_shortest(Decoded const& decoded, u8* buffer) -> Digits {
    i16 k = estimate_scaling_factor(decoded.mant + decoded.plus, decoded.exponent);

    Big mant  = Big::from_u64(decoded.mant);
    Big minus = Big::from_u64(decoded.minus);
    Big plus  = Big::from_u64(decoded.plus);
    Big scale = Big::from_small(1);
    if (decoded.exponent < 0) {
        scale.mul_pow2(static_cast<usize>(-decoded.exponent));
    } else {
        mant.mul_pow2(static_cast<usize>(decoded.exponent));
        minus.mul_pow2(static_cast<usize>(decoded.exponent));
        plus.mul_pow2(static_cast<usize>(decoded.exponent));
    }

    if (k >= 0) {
        mul_pow10(scale, static_cast<usize>(k));
    } else {
        mul_pow10(mant, static_cast<usize>(-k));
        mul_pow10(minus, static_cast<usize>(-k));
        mul_pow10(plus, static_cast<usize>(-k));
    }

    Big upper = mant;
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

    Big scale2 = scale;
    Big scale4 = scale;
    Big scale8 = scale;
    scale2.mul_pow2(1);
    scale4.mul_pow2(2);
    scale8.mul_pow2(3);

    usize len = 0;
    bool  down;
    bool  up;
    for (;;) {
        buffer[len++] = static_cast<u8>('0' + digit(mant, scale, scale2, scale4, scale8));

        down  = decoded.inclusive ? mant.compare(minus) <= 0 : mant.compare(minus) < 0;
        upper = mant;
        upper.add(plus);
        up = decoded.inclusive ? scale.compare(upper) <= 0 : scale.compare(upper) < 0;
        if (down || up) break;

        mant.mul_small(10);
        minus.mul_small(10);
        plus.mul_small(10);
    }

    Big twice_mant = mant;
    twice_mant.mul_pow2(1);
    if (up && (! down || twice_mant.compare(scale) >= 0)) {
        if (const u8 carry = round_up(buffer, len); carry != 0) {
            buffer[len++] = carry;
            ++k;
        }
    }
    return { len, k };
}

auto format_exact(Decoded const& decoded, u8* buffer, usize capacity, i16 limit) -> Digits {
    i16 k = estimate_scaling_factor(decoded.mant, decoded.exponent);

    Big mant  = Big::from_u64(decoded.mant);
    Big scale = Big::from_small(1);
    if (decoded.exponent < 0) {
        scale.mul_pow2(static_cast<usize>(-decoded.exponent));
    } else {
        mant.mul_pow2(static_cast<usize>(decoded.exponent));
    }

    if (k >= 0) {
        mul_pow10(scale, static_cast<usize>(k));
    } else {
        mul_pow10(mant, static_cast<usize>(-k));
    }

    Big half_ulp = scale;
    div_2pow10(half_ulp, capacity);
    half_ulp.add(mant);
    if (half_ulp.compare(scale) >= 0) {
        ++k;
    } else {
        mant.mul_small(10);
    }

    usize len;
    if (k < limit) {
        len = 0;
    } else {
        const i32 requested = static_cast<i32>(k) - limit;
        len = static_cast<usize>(requested) < capacity ? static_cast<usize>(requested) : capacity;
    }

    if (len != 0) {
        Big scale2 = scale;
        Big scale4 = scale;
        Big scale8 = scale;
        scale2.mul_pow2(1);
        scale4.mul_pow2(2);
        scale8.mul_pow2(3);

        for (usize i = 0; i < len; ++i) {
            if (mant.is_zero()) {
                for (; i < len; ++i) buffer[i] = '0';
                return { len, k };
            }
            buffer[i] = static_cast<u8>('0' + digit(mant, scale, scale2, scale4, scale8));
            mant.mul_small(10);
        }
    }

    Big five_scale = scale;
    five_scale.mul_small(5);
    const i8 order = mant.compare(five_scale);
    if (order > 0 || (order == 0 && len != 0 && (buffer[len - 1] & 1) != 0)) {
        if (const u8 carry = round_up(buffer, len); carry != 0) {
            ++k;
            if (k > limit && len < capacity) buffer[len++] = carry;
        }
    }
    return { len, k };
}

auto max_buffer_len(i16 exponent) noexcept -> usize {
    const i32 factor = exponent < 0 ? -12 : 5;
    return 21 + static_cast<usize>(factor * static_cast<i32>(exponent)) / 16;
}

template<usize Capacity>
auto make_decimal(FullDecoded const& decoded) -> Decimal<Capacity> {
    Decimal<Capacity> result {};
    result.category = decoded.category;
    result.negative = decoded.negative;
    return result;
}

} // namespace detail

export template<Float T>
auto shortest(T value) -> ShortestDecimal {
    const auto decoded = detail::decode(value);
    auto       result  = detail::make_decimal<MAX_SIG_DIGITS>(decoded);
    if (decoded.category == Category::Finite) {
        const auto digits = detail::format_shortest(decoded.finite, result.digits.data());
        result.len        = digits.len;
        result.exponent   = digits.exponent;
    }
    return result;
}

export template<Float T>
auto exact_fixed(T value, usize fractional_digits) -> ExactDecimal {
    const auto decoded = detail::decode(value);
    auto       result  = detail::make_decimal<DIGIT_CAPACITY>(decoded);
    if (decoded.category == Category::Finite) {
        const usize capacity = detail::max_buffer_len(decoded.finite.exponent);
        const i16   limit    = fractional_digits < 0x8000
                                   ? static_cast<i16>(-static_cast<i16>(fractional_digits))
                                   : numeric_limits<i16>::min();
        const auto  digits =
            detail::format_exact(decoded.finite, result.digits.data(), capacity, limit);
        result.len      = digits.len;
        result.exponent = digits.exponent;
    }
    return result;
}

export template<Float T>
auto exact_significant(T value, usize significant_digits) -> ExactDecimal {
    const auto decoded = detail::decode(value);
    auto       result  = detail::make_decimal<DIGIT_CAPACITY>(decoded);
    if (decoded.category == Category::Finite) {
        const usize max_len  = detail::max_buffer_len(decoded.finite.exponent);
        const usize capacity = significant_digits < max_len ? significant_digits : max_len;
        const auto  digits   = detail::format_exact(
            decoded.finite, result.digits.data(), capacity, numeric_limits<i16>::min());
        result.len      = digits.len;
        result.exponent = digits.exponent;
    }
    return result;
}

} // namespace rstd::num::flt2dec

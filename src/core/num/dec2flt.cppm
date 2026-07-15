export module rstd.core:num.dec2flt;
import :num.bignum;
import :result;
import :slice;
import rstd.basic;

export namespace rstd::num::dec2flt
{

enum class Error : u8
{
    Invalid,
    Overflow
};

struct Decimal {
    slice<u8> integer;
    slice<u8> fraction;
    i32       exponent = 0;
    bool      negative = false;
};

auto to_f64(Decimal decimal) -> Result<f64, Error>;

} // namespace rstd::num::dec2flt

namespace rstd::num::dec2flt
{

namespace detail
{

constexpr usize MAX_DIGITS = 800;
using Big                  = bignum::FixedBig<128>;

auto digit_at(Decimal const& decimal, usize index) noexcept -> u8 {
    if (index < decimal.integer.len()) return decimal.integer[index];
    return decimal.fraction[index - decimal.integer.len()];
}

auto signed_zero(bool negative) noexcept -> f64 {
    const u64 bits = negative ? u64(1) << 63 : 0;
    return rstd::bit_cast<f64>(bits);
}

auto compare_ratio_to_pow2(Big const& numerator, Big const& denominator, i32 exponent) -> i8 {
    if (exponent >= 0) {
        Big scaled = denominator;
        scaled.mul_pow2(static_cast<usize>(exponent));
        return numerator.compare(scaled);
    }
    Big scaled = numerator;
    scaled.mul_pow2(static_cast<usize>(-exponent));
    return scaled.compare(denominator);
}

struct Division {
    u64 quotient;
    Big remainder;
};

auto divide(Big numerator, Big const& denominator) -> Division {
    u64 quotient = 0;
    if (numerator.compare(denominator) < 0) return { quotient, numerator };

    const usize shift = numerator.bit_length() - denominator.bit_length();
    if (shift >= 64) __builtin_trap();
    for (usize current = shift + 1; current != 0; --current) {
        const usize bit    = current - 1;
        Big         scaled = denominator;
        scaled.mul_pow2(bit);
        if (numerator.compare(scaled) >= 0) {
            numerator.sub(scaled);
            quotient |= u64(1) << bit;
        }
    }
    return { quotient, numerator };
}

auto rounded_quotient(Big numerator, Big denominator, bool sticky) -> u64 {
    auto division = divide(numerator, denominator);
    division.remainder.mul_small(2);
    const i8 order = division.remainder.compare(denominator);
    if (order > 0 || (order == 0 && (sticky || (division.quotient & 1) != 0))) {
        ++division.quotient;
    }
    return division.quotient;
}

auto make_float(Big numerator, Big denominator, bool negative, bool sticky) -> Result<f64, Error> {
    i32 exponent =
        static_cast<i32>(numerator.bit_length()) - static_cast<i32>(denominator.bit_length());
    if (compare_ratio_to_pow2(numerator, denominator, exponent) < 0) --exponent;
    if (exponent > 1023) return Err(Error::Overflow);

    u64 bits;
    if (exponent < -1022) {
        numerator.mul_pow2(1074);
        const u64 mantissa = rounded_quotient(numerator, denominator, sticky);
        bits               = mantissa >= (u64(1) << 52) ? u64(1) << 52 : mantissa;
    } else {
        const i32 shift = 52 - exponent;
        if (shift >= 0) {
            numerator.mul_pow2(static_cast<usize>(shift));
        } else {
            denominator.mul_pow2(static_cast<usize>(-shift));
        }

        u64 mantissa = rounded_quotient(numerator, denominator, sticky);
        if (mantissa == (u64(1) << 53)) {
            mantissa >>= 1;
            ++exponent;
            if (exponent > 1023) return Err(Error::Overflow);
        }
        bits = (static_cast<u64>(exponent + 1023) << 52) | (mantissa - (u64(1) << 52));
    }

    if (negative) bits |= u64(1) << 63;
    return Ok(rstd::bit_cast<f64>(bits));
}

} // namespace detail

auto to_f64(Decimal decimal) -> Result<f64, Error> {
    if (decimal.integer.len() == 0) return Err(Error::Invalid);
    const usize total = decimal.integer.len() + decimal.fraction.len();

    usize first = total;
    usize last  = 0;
    for (usize i = 0; i < total; ++i) {
        const u8 digit = detail::digit_at(decimal, i);
        if (digit < '0' || digit > '9') return Err(Error::Invalid);
        if (digit != '0') {
            if (first == total) first = i;
            last = i;
        }
    }
    if (first == total) return Ok(detail::signed_zero(decimal.negative));

    const usize significant_digits = last - first + 1;
    i64 exponent = static_cast<i64>(decimal.exponent) - static_cast<i64>(decimal.fraction.len()) +
                   static_cast<i64>(total - last - 1);
    const i64 decimal_order = static_cast<i64>(significant_digits - 1) + exponent;
    if (decimal_order > 308) return Err(Error::Overflow);
    if (decimal_order < -324) return Ok(detail::signed_zero(decimal.negative));

    const usize kept =
        significant_digits < detail::MAX_DIGITS ? significant_digits : detail::MAX_DIGITS;
    detail::Big numerator = detail::Big::from_small(0);
    for (usize i = 0; i < kept; ++i) {
        numerator.mul_small(10);
        numerator.add_small(detail::digit_at(decimal, first + i) - '0');
    }

    const bool sticky = kept != significant_digits;
    exponent += static_cast<i64>(significant_digits - kept);
    detail::Big denominator = detail::Big::from_small(1);
    if (exponent >= 0) {
        numerator.mul_pow5(static_cast<usize>(exponent));
        numerator.mul_pow2(static_cast<usize>(exponent));
    } else {
        denominator.mul_pow5(static_cast<usize>(-exponent));
        denominator.mul_pow2(static_cast<usize>(-exponent));
    }
    return detail::make_float(numerator, denominator, decimal.negative, sticky);
}

} // namespace rstd::num::dec2flt

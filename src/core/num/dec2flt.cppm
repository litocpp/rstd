export module rstd.core:num.dec2flt;
import :num.types;
import :num.bignum;
import :intrinsics;
import :result;
import :str.str;
import rstd.basic;

export namespace rstd::num::dec2flt
{

enum class Error : rstd::uint8_t
{
    Invalid,
    Overflow
};

struct Decimal {
    slice<u8> integer;
    slice<u8> fraction;
    i32       exponent {};
    bool      negative = false;
};

auto to_f64(Decimal decimal) -> Result<f64, Error>;

} // namespace rstd::num::dec2flt

using namespace rstd::prelude;
using namespace rstd::num::dec2flt;

constexpr rstd::size_t DECIMAL_MAX_DIGITS = 800;
using DecimalBig                          = rstd::num::bignum::FixedBig<128>;

auto digit_at(Decimal const& decimal, rstd::size_t index) noexcept -> rstd::uint8_t {
    const auto integer_len = decimal.integer.len().to_primitive();
    if (index < integer_len) return decimal.integer[usize(index)].to_primitive();
    return decimal.fraction[usize(index - integer_len)].to_primitive();
}

auto signed_zero(bool negative) noexcept -> f64 {
    const auto bits = negative ? rstd::uint64_t(1) << 63 : rstd::uint64_t(0);
    return f64::from_bits(u64(bits));
}

auto compare_ratio_to_pow2(DecimalBig const& numerator,
                           DecimalBig const& denominator,
                           rstd::int32_t     exponent) -> int {
    if (exponent >= 0) {
        DecimalBig scaled = denominator;
        scaled.mul_pow2(static_cast<rstd::size_t>(exponent));
        return numerator.compare(scaled);
    }
    DecimalBig scaled = numerator;
    scaled.mul_pow2(static_cast<rstd::size_t>(-exponent));
    return scaled.compare(denominator);
}

struct Division {
    rstd::uint64_t quotient;
    DecimalBig     remainder;
};

auto divide(DecimalBig numerator, DecimalBig const& denominator) -> Division {
    rstd::uint64_t quotient = 0;
    if (numerator.compare(denominator) < 0) return { quotient, numerator };

    const rstd::size_t shift = numerator.bit_length() - denominator.bit_length();
    if (shift >= 64) rstd::intrinsics::abort();
    for (rstd::size_t current = shift + 1; current != 0; --current) {
        const rstd::size_t bit    = current - 1;
        DecimalBig         scaled = denominator;
        scaled.mul_pow2(bit);
        if (numerator.compare(scaled) >= 0) {
            numerator.sub(scaled);
            quotient |= rstd::uint64_t(1) << bit;
        }
    }
    return { quotient, numerator };
}

auto rounded_quotient(DecimalBig numerator, DecimalBig denominator, bool sticky) -> rstd::uint64_t {
    auto division = divide(numerator, denominator);
    division.remainder.mul_small(2);
    const int order = division.remainder.compare(denominator);
    if (order > 0 || (order == 0 && (sticky || (division.quotient & 1) != 0))) {
        ++division.quotient;
    }
    return division.quotient;
}

auto make_float(DecimalBig numerator, DecimalBig denominator, bool negative, bool sticky)
    -> rstd::Result<f64, Error> {
    rstd::int32_t exponent = static_cast<rstd::int32_t>(numerator.bit_length()) -
                             static_cast<rstd::int32_t>(denominator.bit_length());
    if (compare_ratio_to_pow2(numerator, denominator, exponent) < 0) --exponent;
    if (exponent > 1023) return rstd::Err(Error::Overflow);

    rstd::uint64_t bits;
    if (exponent < -1022) {
        numerator.mul_pow2(1074);
        const rstd::uint64_t mantissa = rounded_quotient(numerator, denominator, sticky);
        bits = mantissa >= (rstd::uint64_t(1) << 52) ? rstd::uint64_t(1) << 52 : mantissa;
    } else {
        const rstd::int32_t shift = 52 - exponent;
        if (shift >= 0) {
            numerator.mul_pow2(static_cast<rstd::size_t>(shift));
        } else {
            denominator.mul_pow2(static_cast<rstd::size_t>(-shift));
        }

        rstd::uint64_t mantissa = rounded_quotient(numerator, denominator, sticky);
        if (mantissa == (rstd::uint64_t(1) << 53)) {
            mantissa >>= 1;
            ++exponent;
            if (exponent > 1023) return rstd::Err(Error::Overflow);
        }
        bits = (static_cast<rstd::uint64_t>(exponent + 1023) << 52) |
               (mantissa - (rstd::uint64_t(1) << 52));
    }

    if (negative) bits |= rstd::uint64_t(1) << 63;
    return rstd::Ok(f64::from_bits(u64(bits)));
}

namespace rstd::num::dec2flt
{

auto to_f64(Decimal decimal) -> Result<f64, Error> {
    const auto integer_len  = decimal.integer.len().to_primitive();
    const auto fraction_len = decimal.fraction.len().to_primitive();
    if (integer_len == 0) return Err(Error::Invalid);
    const rstd::size_t total = integer_len + fraction_len;

    rstd::size_t first = total;
    rstd::size_t last  = 0;
    for (rstd::size_t i = 0; i < total; ++i) {
        const rstd::uint8_t digit = digit_at(decimal, i);
        if (digit < '0' || digit > '9') return Err(Error::Invalid);
        if (digit != '0') {
            if (first == total) first = i;
            last = i;
        }
    }
    if (first == total) return Ok(signed_zero(decimal.negative));

    const rstd::size_t  significant_digits = last - first + 1;
    rstd::int64_t       exponent = static_cast<rstd::int64_t>(decimal.exponent.to_primitive()) -
                                   static_cast<rstd::int64_t>(fraction_len) +
                                   static_cast<rstd::int64_t>(total - last - 1);
    const rstd::int64_t decimal_order =
        static_cast<rstd::int64_t>(significant_digits - 1) + exponent;
    if (decimal_order > 308) return Err(Error::Overflow);
    if (decimal_order < -324) return Ok(signed_zero(decimal.negative));

    const rstd::size_t kept =
        significant_digits < DECIMAL_MAX_DIGITS ? significant_digits : DECIMAL_MAX_DIGITS;
    DecimalBig numerator = DecimalBig::from_small(0);
    for (rstd::size_t i = 0; i < kept; ++i) {
        numerator.mul_small(10);
        numerator.add_small(digit_at(decimal, first + i) - '0');
    }

    const bool sticky = kept != significant_digits;
    exponent += static_cast<rstd::int64_t>(significant_digits - kept);
    DecimalBig denominator = DecimalBig::from_small(1);
    if (exponent >= 0) {
        numerator.mul_pow5(static_cast<rstd::size_t>(exponent));
        numerator.mul_pow2(static_cast<rstd::size_t>(exponent));
    } else {
        denominator.mul_pow5(static_cast<rstd::size_t>(-exponent));
        denominator.mul_pow2(static_cast<rstd::size_t>(-exponent));
    }
    return make_float(numerator, denominator, decimal.negative, sticky);
}

} // namespace rstd::num::dec2flt

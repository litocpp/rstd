module;
#include <charconv>
#include <cmath>
#include <system_error>

export module rstd.core:fmt.floating;
export import :fmt;

namespace rstd::fmt::float_detail
{

template<typename T>
concept Float = mtp::any<T, f32, f64>;

// 1100 covers binary64's exact fractional tail; the buffer also fits its 309 integral digits.
constexpr usize BUFFER_SIZE           = 1536;
constexpr u16   EXACT_PRECISION_LIMIT = 1100;

auto normalize_exponent(char* begin, char*& end, bool upper) -> char* {
    char* exponent = begin;
    while (exponent != end && *exponent != 'e' && *exponent != 'E') ++exponent;
    if (exponent == end) return end;

    *exponent    = upper ? 'E' : 'e';
    char* source = exponent + 1;
    char* output = source;
    if (*source == '-') {
        *output++ = *source++;
    } else if (*source == '+') {
        ++source;
    }
    while (source + 1 < end && *source == '0') ++source;
    while (source != end) *output++ = *source++;
    end = output;
    return exponent;
}

template<Float T>
auto write(Formatter& formatter, T value, Presentation presentation) -> bool {
    static constexpr u8 MINUS[] = { '-' };
    static constexpr u8 PLUS[]  = { '+' };
    static constexpr u8 NAN_[]  = { 'N', 'a', 'N' };

    if (std::isnan(value)) {
        return formatter.pad_numeric(nullptr, 0, NAN_, sizeof(NAN_), 0, nullptr, 0);
    }

    const bool  negative  = std::signbit(value);
    const u8*   sign      = negative ? MINUS : (formatter.sign_plus() ? PLUS : nullptr);
    const usize sign_len  = sign == nullptr ? 0 : 1;
    const T     magnitude = negative ? -value : value;

    bool scientific =
        presentation == Presentation::LowerExp || presentation == Presentation::UpperExp;
    const bool upper = presentation == Presentation::UpperExp;
    if (presentation == Presentation::Debug && ! formatter.has_prec()) {
        const auto absolute = std::fabs(value);
        scientific          = (absolute != T(0) && absolute < T(1e-4)) || absolute >= T(1e16);
    }

    const auto chars_format = scientific ? std::chars_format::scientific : std::chars_format::fixed;
    const bool exact        = formatter.has_prec();
    u16        precision    = exact ? formatter.precision() : 0;
    usize      extra_zeros  = 0;
    if (exact && std::isfinite(value) && precision > EXACT_PRECISION_LIMIT) {
        extra_zeros = precision - EXACT_PRECISION_LIMIT;
        precision   = EXACT_PRECISION_LIMIT;
    }

    char buffer[BUFFER_SIZE];
    auto result = exact ? std::to_chars(buffer,
                                        buffer + sizeof(buffer),
                                        magnitude,
                                        chars_format,
                                        static_cast<int>(precision))
                        : std::to_chars(buffer, buffer + sizeof(buffer), magnitude, chars_format);
    if (result.ec != std::errc {}) return false;

    char* end = result.ptr;
    if (presentation == Presentation::Debug && ! exact && ! scientific && std::isfinite(value)) {
        bool has_decimal_point = false;
        for (char* current = buffer; current != end; ++current) {
            if (*current == '.') {
                has_decimal_point = true;
                break;
            }
        }
        if (! has_decimal_point) {
            if (end + 2 > buffer + sizeof(buffer)) return false;
            *end++ = '.';
            *end++ = '0';
        }
    }

    char* exponent = scientific ? normalize_exponent(buffer, end, upper) : end;
    return formatter.pad_numeric(sign,
                                 sign_len,
                                 reinterpret_cast<const u8*>(buffer),
                                 static_cast<usize>(exponent - buffer),
                                 extra_zeros,
                                 reinterpret_cast<const u8*>(exponent),
                                 static_cast<usize>(end - exponent));
}

} // namespace rstd::fmt::float_detail

namespace rstd
{

template<fmt::float_detail::Float T>
struct Impl<fmt::Display, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return fmt::float_detail::write(formatter, this->self(), fmt::Presentation::Display);
    }
};

template<fmt::float_detail::Float T>
struct Impl<fmt::Debug, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return fmt::float_detail::write(formatter, this->self(), fmt::Presentation::Debug);
    }
};

template<fmt::float_detail::Float T>
struct Impl<fmt::LowerExp, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return fmt::float_detail::write(formatter, this->self(), fmt::Presentation::LowerExp);
    }
};

template<fmt::float_detail::Float T>
struct Impl<fmt::UpperExp, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return fmt::float_detail::write(formatter, this->self(), fmt::Presentation::UpperExp);
    }
};

} // namespace rstd

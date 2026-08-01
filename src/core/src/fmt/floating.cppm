export module rstd.core:fmt.floating;
import :num.types;
import :intrinsics;
export import :fmt;
import :num.flt2dec;

namespace rstd::fmt::float_detail
{

template<typename T>
concept Float = num::flt2dec::Float<T>;

constexpr rstd::size_t BUFFER_SIZE = 1536;

struct Rendered {
    rstd::size_t len;
    rstd::size_t zero_count;
    rstd::size_t exponent_len;
};

void append(rstd::uint8_t*       buffer,
            rstd::size_t&        len,
            const rstd::uint8_t* source,
            rstd::size_t         source_len) noexcept {
    if (len + source_len > BUFFER_SIZE) rstd::intrinsics::abort();
    for (rstd::size_t i = 0; i < source_len; ++i) buffer[len++] = source[i];
}

void append(rstd::uint8_t* buffer, rstd::size_t& len, rstd::uint8_t value) noexcept {
    if (len == BUFFER_SIZE) rstd::intrinsics::abort();
    buffer[len++] = value;
}

void append_zeros(rstd::uint8_t* buffer, rstd::size_t& len, rstd::size_t count) noexcept {
    if (len + count > BUFFER_SIZE) rstd::intrinsics::abort();
    for (rstd::size_t i = 0; i < count; ++i) buffer[len++] = '0';
}

auto write_exponent(rstd::uint8_t* output, rstd::int32_t exponent, bool upper) noexcept
    -> rstd::size_t {
    rstd::size_t len = 0;
    output[len++]    = upper ? 'E' : 'e';
    if (exponent < 0) {
        output[len++] = '-';
        exponent      = -exponent;
    }

    rstd::uint8_t reversed[4];
    rstd::size_t  digits = 0;
    do {
        reversed[digits++] = static_cast<rstd::uint8_t>('0' + exponent % 10);
        exponent /= 10;
    } while (exponent != 0);
    while (digits != 0) output[len++] = reversed[--digits];
    return len;
}

template<rstd::size_t Capacity>
auto render_fixed(num::flt2dec::Decimal<Capacity> const& decimal,
                  rstd::size_t                           fractional_digits,
                  rstd::uint8_t*                         output) noexcept -> Rendered {
    if (decimal.len == 0) {
        rstd::size_t len = 0;
        append(output, len, '0');
        if (fractional_digits != 0) append(output, len, '.');
        return { len, fractional_digits, 0 };
    }

    rstd::size_t len = 0;
    if (decimal.exponent <= 0) {
        constexpr rstd::uint8_t ZERO_POINT[] = { '0', '.' };
        append(output, len, ZERO_POINT, sizeof(ZERO_POINT));
        const rstd::size_t leading_zeros =
            static_cast<rstd::size_t>(-static_cast<rstd::int32_t>(decimal.exponent));
        append_zeros(output, len, leading_zeros);
        append(output, len, decimal.digits.data(), decimal.len);
        const rstd::size_t rendered_fraction = leading_zeros + decimal.len;
        return { len,
                 fractional_digits > rendered_fraction ? fractional_digits - rendered_fraction : 0,
                 0 };
    }

    const rstd::size_t exponent = static_cast<rstd::size_t>(decimal.exponent);
    if (exponent < decimal.len) {
        append(output, len, decimal.digits.data(), exponent);
        append(output, len, '.');
        append(output, len, decimal.digits.data() + exponent, decimal.len - exponent);
        const rstd::size_t rendered_fraction = decimal.len - exponent;
        return { len,
                 fractional_digits > rendered_fraction ? fractional_digits - rendered_fraction : 0,
                 0 };
    }

    append(output, len, decimal.digits.data(), decimal.len);
    append_zeros(output, len, exponent - decimal.len);
    if (fractional_digits != 0) append(output, len, '.');
    return { len, fractional_digits, 0 };
}

template<rstd::size_t Capacity>
auto render_scientific(num::flt2dec::Decimal<Capacity> const& decimal,
                       rstd::size_t                           significant_digits,
                       bool                                   upper,
                       rstd::uint8_t*                         output,
                       rstd::uint8_t* exponent_output) noexcept -> Rendered {
    rstd::size_t len = 0;
    append(
        output, len, decimal.len == 0 ? static_cast<rstd::uint8_t>('0') : decimal.digits.data()[0]);
    if (decimal.len > 1 || significant_digits > 1) {
        append(output, len, '.');
        if (decimal.len > 1) append(output, len, decimal.digits.data() + 1, decimal.len - 1);
    }

    const rstd::size_t rendered_digits = decimal.len == 0 ? 1 : decimal.len;
    const rstd::size_t zero_count =
        significant_digits > rendered_digits ? significant_digits - rendered_digits : 0;
    const rstd::int32_t exponent =
        decimal.len == 0 ? 0 : static_cast<rstd::int32_t>(decimal.exponent) - 1;
    return { len, zero_count, write_exponent(exponent_output, exponent, upper) };
}

template<Float T, rstd::size_t Capacity>
auto write_decimal(Formatter&                             formatter,
                   T                                      value,
                   Presentation                           presentation,
                   num::flt2dec::Decimal<Capacity> const& decimal,
                   bool                                   exact) -> bool {
    static constexpr rstd::uint8_t MINUS[] = { '-' };
    static constexpr rstd::uint8_t PLUS[]  = { '+' };
    static constexpr rstd::uint8_t NAN_[]  = { 'N', 'a', 'N' };
    static constexpr rstd::uint8_t INF[]   = { 'i', 'n', 'f' };

    if (decimal.category == num::flt2dec::Category::Nan) {
        return formatter.pad_numeric(nullptr, 0, NAN_, sizeof(NAN_), 0, nullptr, 0);
    }

    const rstd::uint8_t* sign = decimal.negative ? MINUS : (formatter.sign_plus() ? PLUS : nullptr);
    const rstd::size_t   sign_len = sign == nullptr ? 0 : 1;
    if (decimal.category == num::flt2dec::Category::Infinite) {
        return formatter.pad_numeric(sign, sign_len, INF, sizeof(INF), 0, nullptr, 0);
    }

    bool scientific =
        presentation == Presentation::LowerExp || presentation == Presentation::UpperExp;
    if (presentation == Presentation::Debug && ! exact) {
        const T magnitude = decimal.negative ? -value : value;
        using Primitive   = typename T::primitive_type;
        scientific        = (magnitude != T(Primitive(0)) && magnitude < T(Primitive(1e-4))) ||
                            magnitude >= T(Primitive(1e16));
    }

    rstd::uint8_t buffer[BUFFER_SIZE];
    rstd::uint8_t exponent[8];
    Rendered      rendered;
    if (scientific) {
        const rstd::size_t significant_digits =
            exact ? static_cast<rstd::size_t>(formatter.precision()) + 1 : 0;
        rendered = render_scientific(
            decimal, significant_digits, presentation == Presentation::UpperExp, buffer, exponent);
    } else {
        const rstd::size_t fractional_digits = exact ? formatter.precision()
                                               : presentation == Presentation::Debug ? 1
                                                                                     : 0;
        rendered                             = render_fixed(decimal, fractional_digits, buffer);
    }

    return formatter.pad_numeric(
        sign, sign_len, buffer, rendered.len, rendered.zero_count, exponent, rendered.exponent_len);
}

template<Float T>
[[gnu::noinline]]
auto write_exact_fixed(Formatter& formatter, T value, Presentation presentation) -> bool {
    const auto decimal = num::flt2dec::exact_fixed(value, formatter.precision());
    return write_decimal(formatter, value, presentation, decimal, true);
}

template<Float T>
[[gnu::noinline]]
auto write_exact_scientific(Formatter& formatter, T value, Presentation presentation) -> bool {
    const auto decimal = num::flt2dec::exact_significant(
        value, static_cast<rstd::size_t>(formatter.precision()) + 1);
    return write_decimal(formatter, value, presentation, decimal, true);
}

template<Float T>
auto write_shortest(Formatter& formatter, T value, Presentation presentation) -> bool {
    const auto decimal = num::flt2dec::shortest(value);
    return write_decimal(formatter, value, presentation, decimal, false);
}

template<Float T>
auto write(Formatter& formatter, T value, Presentation presentation) -> bool {
    if (! formatter.has_prec()) return write_shortest(formatter, value, presentation);
    if (presentation == Presentation::LowerExp || presentation == Presentation::UpperExp) {
        return write_exact_scientific(formatter, value, presentation);
    }
    return write_exact_fixed(formatter, value, presentation);
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

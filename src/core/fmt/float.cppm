export module rstd.core:fmt.floating;
export import :fmt;
import :num.flt2dec;

namespace rstd::fmt::float_detail
{

template<typename T>
concept Float = num::flt2dec::Float<T>;

constexpr usize BUFFER_SIZE = 1536;

struct Rendered {
    usize len;
    usize zero_count;
    usize exponent_len;
};

void append(u8* buffer, usize& len, const u8* source, usize source_len) noexcept {
    if (len + source_len > BUFFER_SIZE) __builtin_trap();
    for (usize i = 0; i < source_len; ++i) buffer[len++] = source[i];
}

void append(u8* buffer, usize& len, u8 value) noexcept {
    if (len == BUFFER_SIZE) __builtin_trap();
    buffer[len++] = value;
}

void append_zeros(u8* buffer, usize& len, usize count) noexcept {
    if (len + count > BUFFER_SIZE) __builtin_trap();
    for (usize i = 0; i < count; ++i) buffer[len++] = '0';
}

auto write_exponent(u8* output, i32 exponent, bool upper) noexcept -> usize {
    usize len     = 0;
    output[len++] = upper ? 'E' : 'e';
    if (exponent < 0) {
        output[len++] = '-';
        exponent      = -exponent;
    }

    u8    reversed[4];
    usize digits = 0;
    do {
        reversed[digits++] = static_cast<u8>('0' + exponent % 10);
        exponent /= 10;
    } while (exponent != 0);
    while (digits != 0) output[len++] = reversed[--digits];
    return len;
}

template<usize Capacity>
auto render_fixed(num::flt2dec::Decimal<Capacity> const& decimal,
                  usize                                  fractional_digits,
                  u8*                                    output) noexcept -> Rendered {
    if (decimal.len == 0) {
        usize len = 0;
        append(output, len, '0');
        if (fractional_digits != 0) append(output, len, '.');
        return { len, fractional_digits, 0 };
    }

    usize len = 0;
    if (decimal.exponent <= 0) {
        append(output, len, reinterpret_cast<const u8*>("0."), 2);
        const usize leading_zeros = static_cast<usize>(-static_cast<i32>(decimal.exponent));
        append_zeros(output, len, leading_zeros);
        append(output, len, decimal.digits.data(), decimal.len);
        const usize rendered_fraction = leading_zeros + decimal.len;
        return { len,
                 fractional_digits > rendered_fraction ? fractional_digits - rendered_fraction : 0,
                 0 };
    }

    const usize exponent = static_cast<usize>(decimal.exponent);
    if (exponent < decimal.len) {
        append(output, len, decimal.digits.data(), exponent);
        append(output, len, '.');
        append(output, len, decimal.digits.data() + exponent, decimal.len - exponent);
        const usize rendered_fraction = decimal.len - exponent;
        return { len,
                 fractional_digits > rendered_fraction ? fractional_digits - rendered_fraction : 0,
                 0 };
    }

    append(output, len, decimal.digits.data(), decimal.len);
    append_zeros(output, len, exponent - decimal.len);
    if (fractional_digits != 0) append(output, len, '.');
    return { len, fractional_digits, 0 };
}

template<usize Capacity>
auto render_scientific(num::flt2dec::Decimal<Capacity> const& decimal,
                       usize                                  significant_digits,
                       bool                                   upper,
                       u8*                                    output,
                       u8* exponent_output) noexcept -> Rendered {
    usize len = 0;
    append(output, len, decimal.len == 0 ? static_cast<u8>('0') : decimal.digits[0]);
    if (decimal.len > 1 || significant_digits > 1) {
        append(output, len, '.');
        if (decimal.len > 1) append(output, len, decimal.digits.data() + 1, decimal.len - 1);
    }

    const usize rendered_digits = decimal.len == 0 ? 1 : decimal.len;
    const usize zero_count =
        significant_digits > rendered_digits ? significant_digits - rendered_digits : 0;
    const i32 exponent = decimal.len == 0 ? 0 : static_cast<i32>(decimal.exponent) - 1;
    return { len, zero_count, write_exponent(exponent_output, exponent, upper) };
}

template<Float T, usize Capacity>
auto write_decimal(Formatter&                             formatter,
                   T                                      value,
                   Presentation                           presentation,
                   num::flt2dec::Decimal<Capacity> const& decimal,
                   bool                                   exact) -> bool {
    static constexpr u8 MINUS[] = { '-' };
    static constexpr u8 PLUS[]  = { '+' };
    static constexpr u8 NAN_[]  = { 'N', 'a', 'N' };
    static constexpr u8 INF[]   = { 'i', 'n', 'f' };

    if (decimal.category == num::flt2dec::Category::Nan) {
        return formatter.pad_numeric(nullptr, 0, NAN_, sizeof(NAN_), 0, nullptr, 0);
    }

    const u8*   sign     = decimal.negative ? MINUS : (formatter.sign_plus() ? PLUS : nullptr);
    const usize sign_len = sign == nullptr ? 0 : 1;
    if (decimal.category == num::flt2dec::Category::Infinite) {
        return formatter.pad_numeric(sign, sign_len, INF, sizeof(INF), 0, nullptr, 0);
    }

    bool scientific =
        presentation == Presentation::LowerExp || presentation == Presentation::UpperExp;
    if (presentation == Presentation::Debug && ! exact) {
        const T magnitude = decimal.negative ? -value : value;
        scientific        = (magnitude != T(0) && magnitude < T(1e-4)) || magnitude >= T(1e16);
    }

    u8       buffer[BUFFER_SIZE];
    u8       exponent[8];
    Rendered rendered;
    if (scientific) {
        const usize significant_digits = exact ? static_cast<usize>(formatter.precision()) + 1 : 0;
        rendered                       = render_scientific(
            decimal, significant_digits, presentation == Presentation::UpperExp, buffer, exponent);
    } else {
        const usize fractional_digits = exact ? formatter.precision()
                                        : presentation == Presentation::Debug ? 1
                                                                              : 0;
        rendered                      = render_fixed(decimal, fractional_digits, buffer);
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
    const auto decimal =
        num::flt2dec::exact_significant(value, static_cast<usize>(formatter.precision()) + 1);
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

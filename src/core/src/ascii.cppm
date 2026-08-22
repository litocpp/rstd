export module rstd.core:ascii;
export import :option;

export namespace rstd::ascii
{

constexpr auto is_digit(u8 value) noexcept -> bool {
    return value >= u8('0') && value <= u8('9');
}

constexpr auto is_hex_digit(u8 value) noexcept -> bool {
    return is_digit(value) || (value >= u8('a') && value <= u8('f')) ||
           (value >= u8('A') && value <= u8('F'));
}

constexpr auto is_alpha(u8 value) noexcept -> bool {
    return (value >= u8('a') && value <= u8('z')) || (value >= u8('A') && value <= u8('Z'));
}

constexpr auto is_alnum(u8 value) noexcept -> bool {
    return is_alpha(value) || is_digit(value);
}

constexpr auto is_lower(u8 value) noexcept -> bool {
    return value >= u8('a') && value <= u8('z');
}

constexpr auto is_upper(u8 value) noexcept -> bool {
    return value >= u8('A') && value <= u8('Z');
}

constexpr auto is_blank(u8 value) noexcept -> bool {
    return value == u8(' ') || value == u8('\t');
}

constexpr auto is_space(u8 value) noexcept -> bool {
    return is_blank(value) || value == u8('\n') || value == u8('\r') || value == u8('\v') ||
           value == u8('\f');
}

constexpr auto digit_value(u8 value, u8 radix) noexcept -> Option<u8> {
    if (radix < u8(2) || radix > u8(36)) return None();

    u8 result {};
    if (is_digit(value)) {
        result = value - u8('0');
    } else if (value >= u8('a') && value <= u8('z')) {
        result = value - u8('a') + u8(10);
    } else if (value >= u8('A') && value <= u8('Z')) {
        result = value - u8('A') + u8(10);
    } else {
        return None();
    }
    if (result >= radix) return None();
    return Some(result);
}

} // namespace rstd::ascii

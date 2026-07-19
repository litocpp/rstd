export module rstd.core:num.literals;
import :num.types;

namespace rstd::num::detail
{

[[noreturn]]
inline void invalid_numeric_literal() {
    __builtin_trap();
}

template<typename T, char... Characters>
consteval auto integer_literal() -> T {
    constexpr char text[] = { Characters..., '\0' };
    using Primitive       = typename T::primitive_type;

    constexpr auto maximum = raw_integer_max<rstd::uint128_t>();
    rstd::size_t   index   = 0;
    unsigned       base    = 10;
    if constexpr (sizeof...(Characters) >= 2) {
        if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            index = 2;
            base  = 16;
        } else if (text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
            index = 2;
            base  = 2;
        } else if (text[0] == '0') {
            index = 1;
            base  = 8;
        }
    }

    rstd::uint128_t value     = 0;
    bool            has_digit = false;
    for (; index < sizeof...(Characters); ++index) {
        const char character = text[index];
        if (character == '\'') continue;
        unsigned digit;
        if (character >= '0' && character <= '9') {
            digit = static_cast<unsigned>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = static_cast<unsigned>(character - 'a') + 10;
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<unsigned>(character - 'A') + 10;
        } else {
            invalid_numeric_literal();
        }
        if (digit >= base || value > (maximum - digit) / base) invalid_numeric_literal();
        value     = value * base + digit;
        has_digit = true;
    }
    if (! has_digit) return T {};
    if (! raw_integer_in_range<Primitive>(value)) invalid_numeric_literal();
    return T(static_cast<Primitive>(value));
}

template<typename T>
consteval auto floating_literal(long double value) -> T {
    using Primitive = typename T::primitive_type;
    if (value > static_cast<long double>(raw_float_traits<Primitive>::max)) {
        invalid_numeric_literal();
    }
    return T(static_cast<Primitive>(value));
}

template<typename T>
consteval auto floating_literal(unsigned long long value) -> T {
    return T(static_cast<typename T::primitive_type>(value));
}

} // namespace rstd::num::detail

export namespace rstd::literals
{

#define RSTD_INTEGER_LITERAL(SUFFIX, TYPE)                          \
    template<char... Characters>                                    \
    consteval auto operator""_##SUFFIX()->TYPE {                    \
        return num::detail::integer_literal<TYPE, Characters...>(); \
    }

RSTD_INTEGER_LITERAL(u8, u8)
RSTD_INTEGER_LITERAL(u16, u16)
RSTD_INTEGER_LITERAL(u32, u32)
RSTD_INTEGER_LITERAL(u64, u64)
RSTD_INTEGER_LITERAL(u128, u128)
RSTD_INTEGER_LITERAL(usize, usize)
RSTD_INTEGER_LITERAL(i8, i8)
RSTD_INTEGER_LITERAL(i16, i16)
RSTD_INTEGER_LITERAL(i32, i32)
RSTD_INTEGER_LITERAL(i64, i64)
RSTD_INTEGER_LITERAL(i128, i128)
RSTD_INTEGER_LITERAL(isize, isize)

#undef RSTD_INTEGER_LITERAL

consteval auto operator""_f32(long double value) -> f32 {
    return num::detail::floating_literal<f32>(value);
}
consteval auto operator""_f32(unsigned long long value) -> f32 {
    return num::detail::floating_literal<f32>(value);
}
consteval auto operator""_f64(long double value) -> f64 {
    return num::detail::floating_literal<f64>(value);
}
consteval auto operator""_f64(unsigned long long value) -> f64 {
    return num::detail::floating_literal<f64>(value);
}

} // namespace rstd::literals

namespace rstd::num::detail
{
using namespace rstd::literals;

static_assert(255_u8 == u8(255));
static_assert(0xffffffffffffffffffffffffffffffff_u128 == u128::MAX);
static_assert(1.5_f32 == f32(1.5f));
} // namespace rstd::num::detail

export module rstd.core:fmt.integer;
import :num.types;
export import :fmt;

template<typename T>
using FormatUnsigned =
    rstd::mtp::cond<(sizeof(T) <= 4),
                    rstd::uint32_t,
                    rstd::mtp::cond<(sizeof(T) <= 8), rstd::uint64_t, rstd::uint128_t>>;

template<typename Unsigned>
auto write_integer_decimal(rstd::fmt::Formatter& formatter, Unsigned value, bool negative) -> bool {
    if constexpr (sizeof(Unsigned) > 8) {
        if (value <= static_cast<Unsigned>(~rstd::uint64_t(0)))
            return write_integer_decimal(formatter, static_cast<rstd::uint64_t>(value), negative);
    }
    static constexpr char pairs[] = "0001020304050607080910111213141516171819"
                                    "2021222324252627282930313233343536373839"
                                    "4041424344454647484950515253545556575859"
                                    "6061626364656667686970717273747576777879"
                                    "8081828384858687888990919293949596979899";
    char                  buffer[sizeof(Unsigned) <= 4 ? 10 : sizeof(Unsigned) <= 8 ? 20 : 39];
    auto                  end    = buffer + sizeof(buffer);
    auto                  cursor = end;
    auto                  pair   = [&](unsigned digits) {
        *--cursor = pairs[digits * 2 + 1];
        *--cursor = pairs[digits * 2];
    };
    if constexpr (sizeof(Unsigned) > 8) {
        constexpr rstd::uint64_t block = 10000000000000000ull;
        while (value > static_cast<Unsigned>(~rstd::uint64_t(0))) {
            auto tail = static_cast<rstd::uint64_t>(value % block);
            value /= block;
            for (unsigned group = 0; group < 4; ++group) {
                auto quad = static_cast<unsigned>(tail % 10000);
                tail /= 10000;
                pair(quad % 100);
                pair(quad / 100);
            }
        }
    }
    using Remaining = rstd::mtp::cond<(sizeof(Unsigned) <= 4), rstd::uint32_t, rstd::uint64_t>;
    auto remaining  = static_cast<Remaining>(value);
    while (remaining >= 10000) {
        auto quad = static_cast<unsigned>(remaining % 10000);
        remaining /= 10000;
        pair(quad % 100);
        pair(quad / 100);
    }
    if (remaining >= 100) {
        pair(static_cast<unsigned>(remaining % 100));
        remaining /= 100;
    }
    if (remaining >= 10)
        pair(static_cast<unsigned>(remaining));
    else
        *--cursor = static_cast<char>('0' + static_cast<unsigned>(remaining));
    rstd::uint8_t sign[1];
    rstd::size_t  sign_length {};
    if (negative)
        sign[sign_length++] = '-';
    else if (formatter.sign_plus())
        sign[sign_length++] = '+';
    return formatter.pad_numeric(sign,
                                 sign_length,
                                 reinterpret_cast<const rstd::uint8_t*>(cursor),
                                 static_cast<rstd::size_t>(end - cursor),
                                 0,
                                 nullptr,
                                 0);
}

template<typename Unsigned>
auto write_integer_radix(rstd::fmt::Formatter&   formatter,
                         Unsigned                value,
                         rstd::fmt::Presentation presentation) -> bool {
    const bool     octal = presentation == rstd::fmt::Presentation::Octal;
    const unsigned shift = octal ? 3 : 4;
    const unsigned mask  = (1u << shift) - 1;
    const char*    digits =
        presentation == rstd::fmt::Presentation::UpperHex ? "0123456789ABCDEF" : "0123456789abcdef";
    rstd::uint8_t buffer[(sizeof(Unsigned) * 8 + 2) / 3];
    auto          offset = sizeof(buffer);
    do {
        buffer[--offset] = static_cast<rstd::uint8_t>(digits[static_cast<unsigned>(value & mask)]);
        value >>= shift;
    } while (value != 0);
    rstd::uint8_t prefix[3];
    rstd::size_t  length {};
    if (formatter.sign_plus()) prefix[length++] = '+';
    if (formatter.alternate()) {
        prefix[length++] = '0';
        prefix[length++] = octal ? 'o' : 'x';
    }
    return formatter.pad_numeric(
        prefix, length, buffer + offset, sizeof(buffer) - offset, 0, nullptr, 0);
}

template<typename T>
    requires(rstd::num::Integer<T> || rstd::is_raw_int<T>)
auto integer_bits(T value) {
    auto raw = [&] {
        if constexpr (rstd::num::Integer<T>)
            return value.to_primitive();
        else
            return value;
    }();
    using Unsigned = FormatUnsigned<decltype(raw)>;
    // Preserve the source width for signed hexadecimal and octal output.
    constexpr auto mask = ~Unsigned(0) >> ((sizeof(Unsigned) - sizeof(raw)) * 8);
    return static_cast<Unsigned>(raw) & mask;
}

namespace rstd
{

template<typename T>
    requires(num::Integer<T> || rstd::is_raw_int<T>)
struct Impl<fmt::Display, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto raw = [&] {
            if constexpr (num::Integer<T>)
                return this->self().to_primitive();
            else
                return this->self();
        }();
        using Raw      = decltype(raw);
        using Unsigned = FormatUnsigned<Raw>;
        auto magnitude = static_cast<Unsigned>(raw);
        bool negative {};
        if constexpr (Raw(-1) < Raw(0)) {
            negative = raw < 0;
            if (negative) magnitude = Unsigned(0) - magnitude;
        }
        return write_integer_decimal(formatter, magnitude, negative);
    }
};

template<typename T>
    requires(num::Integer<T> || rstd::is_raw_int<T>)
struct Impl<fmt::Debug, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<typename T>
    requires(num::Integer<T> || rstd::is_raw_int<T>)
struct Impl<fmt::LowerHex, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return write_integer_radix(
            formatter, integer_bits(this->self()), fmt::Presentation::LowerHex);
    }
};

template<typename T>
    requires(num::Integer<T> || rstd::is_raw_int<T>)
struct Impl<fmt::UpperHex, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return write_integer_radix(
            formatter, integer_bits(this->self()), fmt::Presentation::UpperHex);
    }
};

template<typename T>
    requires(num::Integer<T> || rstd::is_raw_int<T>)
struct Impl<fmt::Octal, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return write_integer_radix(formatter, integer_bits(this->self()), fmt::Presentation::Octal);
    }
};

} // namespace rstd

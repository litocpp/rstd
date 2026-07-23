module;
#include <rstd/enum.hpp>

export module rstd.core:num.integer;
import :num.types;
export import :num.convert;
export import :option;
export import :str.traits;
export import :trait;
export import :intrinsics;
import :enum_;
import :convert;

export namespace rstd::num
{

class IntErrorKind final {
    RSTD_ENUM(IntErrorKind, (Empty), (InvalidDigit), (PosOverflow), (NegOverflow), (Zero))
};

class ParseIntError {
    IntErrorKind kind_;

    explicit constexpr ParseIntError(IntErrorKind kind) noexcept: kind_(rstd::move(kind)) {}

    template<typename, typename>
    friend struct rstd::Impl;

public:
    [[nodiscard]]
    constexpr auto kind() const noexcept -> ref<IntErrorKind> {
        return ref<IntErrorKind>::from_raw_parts(&kind_);
    }
};

} // namespace rstd::num

namespace rstd
{
template<num::Integer T>
struct Impl<str_::FromStr, T> {
    using Err  = num::ParseIntError;
    using Self = T;

    static auto from_str(ref<str> input) -> Result<Self, Err> {
        auto error = [](num::IntErrorKind kind) -> Result<Self, Err> {
            return rstd::Err(Err(rstd::move(kind)));
        };

        if (input.size() == usize()) return error(num::IntErrorKind::Empty());

        rstd::size_t index    = 0;
        bool         negative = false;
        if (input[usize()] == u8('+')) {
            index = rstd::size_t(1);
        } else if (input[usize()] == u8('-')) {
            if constexpr (! T::IS_SIGNED) {
                return error(num::IntErrorKind::InvalidDigit());
            } else {
                negative = true;
                index    = rstd::size_t(1);
            }
        }

        if (index == input.size().to_primitive()) return error(num::IntErrorKind::InvalidDigit());

        auto const positive_limit = static_cast<rstd::uint128_t>(T::MAX.to_primitive());
        auto const limit =
            negative ? positive_limit + static_cast<rstd::uint128_t>(1) : positive_limit;
        rstd::uint128_t value = 0;

        for (; index < input.size().to_primitive(); ++index) {
            rstd::uint8_t const byte = input[usize(index)].to_primitive();
            if (byte < '0' || byte > '9') {
                return error(num::IntErrorKind::InvalidDigit());
            }

            auto const digit = static_cast<rstd::uint128_t>(byte - '0');
            if (value > (limit - digit) / static_cast<rstd::uint128_t>(10)) {
                return error(negative ? num::IntErrorKind::NegOverflow()
                                      : num::IntErrorKind::PosOverflow());
            }
            value = value * static_cast<rstd::uint128_t>(10) + digit;
        }

        using Primitive = typename T::primitive_type;
        if constexpr (T::IS_SIGNED) {
            if (negative) {
                if (value == limit) return Ok(T::MIN);
                return Ok(T(static_cast<Primitive>(-static_cast<rstd::int128_t>(value))));
            }
        }
        return Ok(T(static_cast<Primitive>(value)));
    }
};

template<>
struct Impl<fmt::Display, num::ParseIntError> : ImplBase<num::ParseIntError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const char* message = "number would be zero for non-zero type";
        switch (this->self().kind()->tag()) {
        case num::IntErrorKind::Tag::Empty:
            message = "cannot parse integer from empty string";
            break;
        case num::IntErrorKind::Tag::InvalidDigit: message = "invalid digit found in string"; break;
        case num::IntErrorKind::Tag::PosOverflow:
            message = "number too large to fit in target type";
            break;
        case num::IntErrorKind::Tag::NegOverflow:
            message = "number too small to fit in target type";
            break;
        case num::IntErrorKind::Tag::Zero: break;
        }
        return formatter.write_raw(message, rstd::strlen(message));
    }
};

template<>
struct Impl<fmt::Debug, num::IntErrorKind> : ImplBase<num::IntErrorKind> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        const char* name = "Zero";
        switch (this->self().tag()) {
        case num::IntErrorKind::Tag::Empty: name = "Empty"; break;
        case num::IntErrorKind::Tag::InvalidDigit: name = "InvalidDigit"; break;
        case num::IntErrorKind::Tag::PosOverflow: name = "PosOverflow"; break;
        case num::IntErrorKind::Tag::NegOverflow: name = "NegOverflow"; break;
        case num::IntErrorKind::Tag::Zero: break;
        }
        return formatter.write_raw(reinterpret_cast<rstd::uint8_t const*>(name),
                                   rstd::strlen(name));
    }
};

template<>
struct Impl<fmt::Debug, num::ParseIntError> : ImplBase<num::ParseIntError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (! formatter.write_raw(reinterpret_cast<rstd::uint8_t const*>("ParseIntError { kind: "),
                                  rstd::size_t(22))) {
            return false;
        }
        if (! as<fmt::Debug>(*this->self().kind()).fmt(formatter)) return false;
        return formatter.write_raw(reinterpret_cast<rstd::uint8_t const*>(" }"), rstd::size_t(2));
    }
};

} // namespace rstd

#undef RSTD_INT_ERROR_KINDS

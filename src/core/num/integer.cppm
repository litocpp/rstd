module;
#include <rstd/enum.hpp>
#define INT_IMPL(T) \
    template<>      \
    struct Impl<T, T> : IntImpl<T> {}

export module rstd.core:num.integer;
export import :option;
export import :str.traits;
export import :trait;
export import :intrinsics;
import :enum_;
import :convert;

#define RSTD_INT_ERROR_KINDS(V) \
    V(Empty)                    \
    V(InvalidDigit)             \
    V(PosOverflow)              \
    V(NegOverflow)              \
    V(Zero)

export namespace rstd::num
{

RSTD_TAG_ENUM(IntErrorKind, RSTD_INT_ERROR_KINDS)

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

class TryFromIntError {
    constexpr TryFromIntError() noexcept = default;

    template<typename, typename>
    friend struct rstd::Impl;

public:
    constexpr TryFromIntError(const TryFromIntError&) noexcept = default;
    constexpr TryFromIntError(TryFromIntError&&) noexcept      = default;
};

} // namespace rstd::num

template<typename T>
concept rstd_integer =
    rstd::mtp::is_int<T> && (! rstd::mtp::same_as<T, bool>) && (! rstd::mtp::same_as<T, char>) &&
    (! rstd::mtp::same_as<T, wchar_t>) && (! rstd::mtp::same_as<T, char8_t>) &&
    (! rstd::mtp::same_as<T, char16_t>) && (! rstd::mtp::same_as<T, char32_t>);

namespace rstd
{
template<typename T>
struct IntImpl : ImplBase<T> {
    using Self = T;
    auto checked_add(Self rhs) const noexcept -> Option<Self> {
        auto [a, b] = overflowing_add(rhs);
        if (b) [[unlikely]] {
            return None();
        } else {
            return Some(a);
        }
    }

    auto overflowing_add(Self rhs) const noexcept -> rstd::tuple<Self, bool> {
        return intrinsics::add_with_overflow(this->self(), rhs);
    }
};

INT_IMPL(u32);
INT_IMPL(u64);

template<rstd_integer From, rstd_integer To>
    requires(! mtp::same_as<From, To>)
struct Impl<convert::TryFrom<From>, To> {
private:
    static constexpr bool infallible =
        (! numeric_limits<From>::is_signed || numeric_limits<To>::is_signed) &&
        numeric_limits<To>::digits >= numeric_limits<From>::digits;

public:
    using Error = mtp::cond<infallible, convert::Infallible, num::TryFromIntError>;

    static auto try_from(From value) -> Result<To, Error> {
        if constexpr (infallible) {
            return Ok(static_cast<To>(value));
        } else {
            bool out_of_range = false;
            if constexpr (numeric_limits<From>::is_signed) {
                if (value < 0) {
                    if constexpr (numeric_limits<To>::is_signed) {
                        out_of_range =
                            static_cast<i128>(value) < static_cast<i128>(numeric_limits<To>::min());
                    } else {
                        out_of_range = true;
                    }
                } else {
                    out_of_range =
                        static_cast<u128>(value) > static_cast<u128>(numeric_limits<To>::max());
                }
            } else {
                out_of_range =
                    static_cast<u128>(value) > static_cast<u128>(numeric_limits<To>::max());
            }

            if (out_of_range) return Err(num::TryFromIntError {});
            return Ok(static_cast<To>(value));
        }
    }
};

template<rstd_integer T>
struct Impl<str_::FromStr, T> {
    using Err  = num::ParseIntError;
    using Self = T;

    static auto from_str(ref<str> input) -> Result<Self, Err> {
        auto error = [](num::IntErrorKind kind) -> Result<Self, Err> {
            return rstd::Err(Err(rstd::move(kind)));
        };

        if (input.size() == 0) return error(num::IntErrorKind::Empty());

        usize index    = 0;
        bool  negative = false;
        if (input.data()[0] == '+') {
            index = 1;
        } else if (input.data()[0] == '-') {
            if constexpr (! numeric_limits<T>::is_signed) {
                return error(num::IntErrorKind::InvalidDigit());
            } else {
                negative = true;
                index    = 1;
            }
        }

        if (index == input.size()) return error(num::IntErrorKind::InvalidDigit());

        const u128 positive_limit = static_cast<u128>(numeric_limits<T>::max());
        const u128 limit          = negative ? positive_limit + 1 : positive_limit;
        u128       value          = 0;

        for (; index < input.size(); ++index) {
            const u8 byte = input.data()[index];
            if (byte < '0' || byte > '9') {
                return error(num::IntErrorKind::InvalidDigit());
            }

            const u128 digit = static_cast<u128>(byte - '0');
            if (value > (limit - digit) / 10) {
                return error(negative ? num::IntErrorKind::NegOverflow()
                                      : num::IntErrorKind::PosOverflow());
            }
            value = value * 10 + digit;
        }

        if constexpr (numeric_limits<T>::is_signed) {
            if (negative) {
                if (value == limit) return Ok(numeric_limits<T>::min());
                return Ok(static_cast<T>(-static_cast<i128>(value)));
            }
        }
        return Ok(static_cast<T>(value));
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
        return formatter.pad(message);
    }
};

template<>
struct Impl<fmt::Display, num::TryFromIntError> : ImplBase<num::TryFromIntError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.pad("out of range integral type conversion attempted");
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
        return formatter.write_raw(reinterpret_cast<const u8*>(name), rstd::strlen(name));
    }
};

template<>
struct Impl<fmt::Debug, num::ParseIntError> : ImplBase<num::ParseIntError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (! formatter.write_raw(reinterpret_cast<const u8*>("ParseIntError { kind: "), 22)) {
            return false;
        }
        if (! as<fmt::Debug>(*this->self().kind()).fmt(formatter)) return false;
        return formatter.write_raw(reinterpret_cast<const u8*>(" }"), 2);
    }
};

} // namespace rstd

#undef RSTD_INT_ERROR_KINDS

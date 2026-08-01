module;
#include <type_traits>

export module rstd.core:num.convert;
import :num.types;
import :error.trait;
export import :convert;
export import :fmt;

export namespace rstd::num
{
class TryFromIntError {
    constexpr TryFromIntError() noexcept = default;

    template<typename, typename>
    friend struct rstd::Impl;

public:
    constexpr TryFromIntError(const TryFromIntError&) noexcept = default;
    constexpr TryFromIntError(TryFromIntError&&) noexcept      = default;
};

class TryFromFloatError {
    constexpr TryFromFloatError() noexcept = default;

    template<typename, typename>
    friend struct rstd::Impl;

public:
    constexpr TryFromFloatError(const TryFromFloatError&) noexcept = default;
    constexpr TryFromFloatError(TryFromFloatError&&) noexcept      = default;
};
} // namespace rstd::num

namespace rstd::num::detail
{
template<typename T>
concept IntegerLike =
    rstd::num::Integer<T> || PrimitiveInteger<T> ||
    (mtp::is_enum<mtp::rm_cvf<T>> && PrimitiveInteger<mtp::underlying<mtp::rm_cvf<T>>>);

template<typename T>
concept FloatLike = rstd::num::Float<T> || PrimitiveFloat<T>;

template<typename T>
struct numeric_primitive {
    using type = mtp::rm_cvf<T>;
};

template<rstd::num::Integer T>
struct numeric_primitive<T> {
    using type = typename mtp::rm_cvf<T>::primitive_type;
};

template<rstd::num::Float T>
struct numeric_primitive<T> {
    using type = typename mtp::rm_cvf<T>::primitive_type;
};

template<typename T>
    requires mtp::is_enum<mtp::rm_cvf<T>>
struct numeric_primitive<T> {
    using type = mtp::underlying<mtp::rm_cvf<T>>;
};

template<typename T>
using numeric_primitive_t = typename numeric_primitive<mtp::rm_cvf<T>>::type;

template<IntegerLike T>
constexpr auto integer_value(T value) noexcept -> numeric_primitive_t<T> {
    if constexpr (rstd::num::Integer<T>) {
        return value.to_primitive();
    } else if constexpr (mtp::is_enum<mtp::rm_cvf<T>>) {
        return static_cast<numeric_primitive_t<T>>(value);
    } else {
        return value;
    }
}

template<FloatLike T>
constexpr auto floating_value(T value) noexcept -> numeric_primitive_t<T> {
    if constexpr (rstd::num::Float<T>) {
        return value.to_primitive();
    } else {
        return value;
    }
}

template<IntegerLike T>
inline constexpr bool integer_signed = raw_integer_signed<numeric_primitive_t<T>>;

template<IntegerLike T>
inline constexpr int integer_digits = raw_integer_digits<numeric_primitive_t<T>>;

template<FloatLike T>
inline constexpr int float_digits = raw_float_traits<numeric_primitive_t<T>>::mantissa_digits;

template<FloatLike T>
inline constexpr int float_max_exponent = raw_float_traits<numeric_primitive_t<T>>::max_exponent;

template<IntegerLike From, IntegerLike To>
inline constexpr bool lossless_integer_conversion =
    (integer_signed<From> == integer_signed<To> && integer_digits<To> >= integer_digits<From>) ||
    (! integer_signed<From> && integer_signed<To> && integer_digits<To> >= integer_digits<From>);

template<FloatLike From, FloatLike To>
inline constexpr bool lossless_float_conversion =
    float_digits<To> >= float_digits<From> && float_max_exponent<To> >= float_max_exponent<From>;

template<IntegerLike From, FloatLike To>
inline constexpr bool lossless_integer_to_float_conversion =
    float_digits<To> >= integer_digits<From> && float_max_exponent<To> > integer_digits<From>;

template<typename From, typename To>
inline constexpr bool lossless_numeric_conversion = [] {
    if constexpr (IntegerLike<From> && IntegerLike<To>) {
        return lossless_integer_conversion<From, To>;
    } else if constexpr (IntegerLike<From> && FloatLike<To>) {
        return lossless_integer_to_float_conversion<From, To>;
    } else if constexpr (FloatLike<From> && FloatLike<To>) {
        return lossless_float_conversion<From, To>;
    } else {
        return false;
    }
}();

template<IntegerLike To, IntegerLike From>
constexpr auto integer_from(From value) noexcept -> To {
    using Primitive      = numeric_primitive_t<To>;
    auto const converted = static_cast<Primitive>(integer_value(value));
    if constexpr (rstd::num::Integer<To>) {
        return To(converted);
    } else if constexpr (mtp::is_enum<mtp::rm_cvf<To>>) {
        return static_cast<To>(converted);
    } else {
        return converted;
    }
}

template<FloatLike To, typename Primitive>
constexpr auto float_from_primitive(Primitive value) noexcept -> To {
    using TargetPrimitive = numeric_primitive_t<To>;
    auto converted        = static_cast<TargetPrimitive>(value);
    if constexpr (rstd::num::Float<To>) {
        return To(converted);
    } else {
        return converted;
    }
}

template<IntegerLike To, IntegerLike From>
constexpr auto integer_in_range(From value) noexcept -> bool {
    using ToPrimitive = numeric_primitive_t<To>;
    return raw_integer_in_range<ToPrimitive>(integer_value(value));
}

template<typename Primitive>
struct unsigned_primitive_for_cast {
    using type = std::make_unsigned_t<Primitive>;
};

template<>
struct unsigned_primitive_for_cast<rstd::int128_t> {
    using type = rstd::uint128_t;
};

template<>
struct unsigned_primitive_for_cast<rstd::uint128_t> {
    using type = rstd::uint128_t;
};

template<typename Primitive>
using unsigned_primitive_for_cast_t = typename unsigned_primitive_for_cast<Primitive>::type;

template<IntegerLike To, IntegerLike From>
constexpr auto lossy_integer_cast(From value) noexcept -> To {
    using ToPrimitive = numeric_primitive_t<To>;
    using ToUnsigned  = unsigned_primitive_for_cast_t<ToPrimitive>;
    auto const  bits  = static_cast<ToUnsigned>(integer_value(value));
    ToPrimitive converted;
    if constexpr (raw_integer_signed<ToPrimitive>) {
        converted = __builtin_bit_cast(ToPrimitive, bits);
    } else {
        converted = bits;
    }

    if constexpr (rstd::num::Integer<To>) {
        return To(converted);
    } else if constexpr (mtp::is_enum<mtp::rm_cvf<To>>) {
        return static_cast<To>(converted);
    } else {
        return converted;
    }
}

template<IntegerLike To, FloatLike From>
constexpr auto lossy_float_to_integer(From value) noexcept -> To {
    using ToPrimitive        = numeric_primitive_t<To>;
    constexpr int     digits = raw_integer_digits<ToPrimitive>;
    const long double raw    = static_cast<long double>(floating_value(value));

    if (raw != raw) return integer_from<To>(ToPrimitive(0));

    const long double upper = __builtin_ldexpl(1.0L, digits);
    if constexpr (raw_integer_signed<ToPrimitive>) {
        if (raw <= -upper) return integer_from<To>(raw_integer_min<ToPrimitive>());
        if (raw >= upper) return integer_from<To>(raw_integer_max<ToPrimitive>());
    } else {
        if (raw <= 0.0L) return integer_from<To>(ToPrimitive(0));
        if (raw >= upper) return integer_from<To>(raw_integer_max<ToPrimitive>());
    }

    return integer_from<To>(static_cast<ToPrimitive>(__builtin_truncl(raw)));
}

template<IntegerLike To, FloatLike From>
constexpr auto exact_float_to_integer(From value) noexcept -> rstd::tuple<To, bool> {
    using ToPrimitive        = numeric_primitive_t<To>;
    constexpr int     digits = raw_integer_digits<ToPrimitive>;
    const long double raw    = static_cast<long double>(floating_value(value));
    if (raw != raw || __builtin_isinf(raw) || __builtin_truncl(raw) != raw) {
        return { integer_from<To>(ToPrimitive(0)), false };
    }

    const long double upper = __builtin_ldexpl(1.0L, digits);
    if constexpr (raw_integer_signed<ToPrimitive>) {
        if (raw < -upper || raw >= upper) return { integer_from<To>(ToPrimitive(0)), false };
    } else {
        if (raw < 0.0L || raw >= upper) return { integer_from<To>(ToPrimitive(0)), false };
    }
    return { integer_from<To>(static_cast<ToPrimitive>(raw)), true };
}

template<FloatLike To, IntegerLike From>
constexpr auto exact_integer_to_float(From value) noexcept -> rstd::tuple<To, bool> {
    auto converted = float_from_primitive<To>(integer_value(value));
    if (__builtin_isinf(floating_value(converted))) return { converted, false };
    auto [roundtrip, valid] = exact_float_to_integer<From>(converted);
    return { converted, valid && integer_value(roundtrip) == integer_value(value) };
}

template<FloatLike To, FloatLike From>
constexpr auto exact_float_conversion(From value) noexcept -> rstd::tuple<To, bool> {
    auto       converted = float_from_primitive<To>(floating_value(value));
    const auto raw       = floating_value(value);
    if (__builtin_isnan(raw)) return { converted, false };
    const auto roundtrip = static_cast<numeric_primitive_t<From>>(floating_value(converted));
    return { converted, roundtrip == raw };
}
} // namespace rstd::num::detail

namespace rstd
{
template<typename From, typename To>
    requires num::detail::lossless_numeric_conversion<From, To>
struct Impl<convert::Into<To>, From> : ImplBase<From> {
    auto into() -> To { return Impl<convert::From<From>, To>::from(rstd::move(this->self())); }
};

template<typename From, typename To>
    requires((num::detail::IntegerLike<From> || num::detail::FloatLike<From>) &&
             (num::detail::IntegerLike<To> || num::detail::FloatLike<To>))
struct Impl<convert::TryInto<To>, From> : ImplBase<From> {
    using Error = typename Impl<convert::TryFrom<From>, To>::Error;

    auto try_into() -> Result<To, Error> {
        return Impl<convert::TryFrom<From>, To>::try_from(rstd::move(this->self()));
    }
};

template<num::detail::IntegerLike From, num::detail::IntegerLike To>
    requires num::detail::lossless_integer_conversion<From, To>
struct Impl<convert::From<From>, To> {
    static constexpr auto from(From value) -> To { return num::detail::integer_from<To>(value); }
};

template<num::detail::IntegerLike From, num::detail::IntegerLike To>
    requires num::detail::lossless_integer_conversion<From, To>
struct Impl<convert::TryFrom<From>, To> {
    using Error = convert::Infallible;

    static constexpr auto try_from(From value) -> Result<To, Error> {
        return Ok(num::detail::integer_from<To>(value));
    }
};

template<num::detail::IntegerLike From, num::detail::IntegerLike To>
    requires(! num::detail::lossless_integer_conversion<From, To>)
struct Impl<convert::TryFrom<From>, To> {
    using Error = num::TryFromIntError;

    static constexpr auto try_from(From value) -> Result<To, Error> {
        if (! num::detail::integer_in_range<To>(value)) return Err(Error {});
        return Ok(num::detail::integer_from<To>(value));
    }
};

template<num::detail::IntegerLike To, num::detail::IntegerLike From>
struct AsCast<To, From> {
    static constexpr auto cast(From value) noexcept -> To {
        return num::detail::lossy_integer_cast<To>(value);
    }
};

template<num::detail::IntegerLike From, num::detail::FloatLike To>
    requires num::detail::lossless_integer_to_float_conversion<From, To>
struct Impl<convert::From<From>, To> {
    static constexpr auto from(From value) -> To {
        return num::detail::float_from_primitive<To>(num::detail::integer_value(value));
    }
};

template<num::detail::IntegerLike From, num::detail::FloatLike To>
    requires num::detail::lossless_integer_to_float_conversion<From, To>
struct Impl<convert::TryFrom<From>, To> {
    using Error = convert::Infallible;

    static constexpr auto try_from(From value) -> Result<To, Error> {
        return Ok(num::detail::float_from_primitive<To>(num::detail::integer_value(value)));
    }
};

template<num::detail::IntegerLike From, num::detail::FloatLike To>
    requires(! num::detail::lossless_integer_to_float_conversion<From, To>)
struct Impl<convert::TryFrom<From>, To> {
    using Error = num::TryFromFloatError;

    static constexpr auto try_from(From value) -> Result<To, Error> {
        auto [converted, exact] = num::detail::exact_integer_to_float<To>(value);
        if (! exact) return Err(Error {});
        return Ok(converted);
    }
};

template<num::detail::FloatLike From, num::detail::FloatLike To>
    requires num::detail::lossless_float_conversion<From, To>
struct Impl<convert::From<From>, To> {
    static constexpr auto from(From value) -> To {
        return num::detail::float_from_primitive<To>(num::detail::floating_value(value));
    }
};

template<num::detail::FloatLike From, num::detail::FloatLike To>
    requires num::detail::lossless_float_conversion<From, To>
struct Impl<convert::TryFrom<From>, To> {
    using Error = convert::Infallible;

    static constexpr auto try_from(From value) -> Result<To, Error> {
        return Ok(num::detail::float_from_primitive<To>(num::detail::floating_value(value)));
    }
};

template<num::detail::FloatLike From, num::detail::FloatLike To>
    requires(! num::detail::lossless_float_conversion<From, To>)
struct Impl<convert::TryFrom<From>, To> {
    using Error = num::TryFromFloatError;

    static constexpr auto try_from(From value) -> Result<To, Error> {
        auto [converted, exact] = num::detail::exact_float_conversion<To>(value);
        if (! exact) return Err(Error {});
        return Ok(converted);
    }
};

template<num::detail::FloatLike From, num::detail::IntegerLike To>
struct Impl<convert::TryFrom<From>, To> {
    using Error = num::TryFromIntError;

    static constexpr auto try_from(From value) -> Result<To, Error> {
        auto [converted, exact] = num::detail::exact_float_to_integer<To>(value);
        if (! exact) return Err(Error {});
        return Ok(converted);
    }
};

template<num::detail::FloatLike To, num::detail::IntegerLike From>
struct AsCast<To, From> {
    static constexpr auto cast(From value) noexcept -> To {
        return num::detail::float_from_primitive<To>(num::detail::integer_value(value));
    }
};

template<num::detail::IntegerLike To, num::detail::FloatLike From>
struct AsCast<To, From> {
    static constexpr auto cast(From value) noexcept -> To {
        return num::detail::lossy_float_to_integer<To>(value);
    }
};

template<num::detail::FloatLike To, num::detail::FloatLike From>
struct AsCast<To, From> {
    static constexpr auto cast(From value) noexcept -> To {
        return num::detail::float_from_primitive<To>(num::detail::floating_value(value));
    }
};

template<>
struct Impl<fmt::Display, num::TryFromIntError> : ImplBase<num::TryFromIntError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_raw("out of range integral type conversion attempted",
                                   sizeof("out of range integral type conversion attempted") - 1);
    }
};

template<>
struct Impl<fmt::Display, num::TryFromFloatError> : ImplBase<num::TryFromFloatError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_raw("inexact floating point conversion attempted",
                                   sizeof("inexact floating point conversion attempted") - 1);
    }
};

template<>
struct Impl<fmt::Debug, num::TryFromIntError> : ImplBase<num::TryFromIntError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_raw("TryFromIntError", sizeof("TryFromIntError") - 1);
    }
};

template<>
struct Impl<fmt::Debug, num::TryFromFloatError> : ImplBase<num::TryFromFloatError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_raw("TryFromFloatError", sizeof("TryFromFloatError") - 1);
    }
};

template<>
struct Impl<error::Error, num::TryFromIntError> : ImplBase<num::TryFromIntError> {
    auto source() const noexcept -> Option<error::ErrorRef> { return None(); }
};

template<>
struct Impl<error::Error, num::TryFromFloatError> : ImplBase<num::TryFromFloatError> {
    auto source() const noexcept -> Option<error::ErrorRef> { return None(); }
};
} // namespace rstd

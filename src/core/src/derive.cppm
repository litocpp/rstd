export module rstd.core:derive;
export import :fmt.debug;
export import :str.traits;
import :error;
import :fmt.integer;
export import :enum_;
import :array;
import :num.convert;
using namespace rstd::literals;

export namespace rstd::derive
{
template<str_::fixed_string Text>
constexpr auto text() -> ref<str> {
    return literals::operator""_str < Text>();
}

template<str_::fixed_string Name, auto Getter>
struct Field {
    static constexpr auto name() -> ref<str> { return text<Name>(); }
    template<typename T>
    static constexpr auto get(const T& value) -> decltype(auto) {
        return Getter(value);
    }
};

template<typename T, str_::fixed_string Name, typename... Fields>
struct DebugRecord {
    static auto fmt(const T& value, fmt::Formatter& formatter) -> bool {
        fmt::DebugStruct output(formatter, text<Name>());
        (output.field(Fields::name(), Fields::get(value)), ...);
        return output.finish();
    }
};

template<typename T, str_::fixed_string Format, typename... Fields>
struct DisplayRecord {
    static auto fmt(const T& value, fmt::Formatter& formatter) -> bool {
        return formatter.write_fmt(fmt::Arguments::make(Format.data, Fields::get(value)...));
    }
};

template<typename T, typename Member>
struct TransparentDisplay {
    static auto fmt(const T& value, fmt::Formatter& formatter) -> bool {
        const auto& field = Member::get(value);
        return as<fmt::Display>(field).fmt(formatter);
    }
};

enum class Case
{
    Sensitive,
    AsciiInsensitive
};

constexpr auto equal(ref<str> left, ref<str> right, Case matching) -> bool {
    if (left.len() != right.len()) return false;
    for (usize index {}; index < left.len(); ++index) {
        auto a = left[index].to_primitive();
        auto b = right[index].to_primitive();
        if (matching == Case::AsciiInsensitive) {
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        }
        if (a != b) return false;
    }
    return true;
}

template<auto Value, str_::fixed_string Name, str_::fixed_string... Aliases>
struct EnumEntry {
    static constexpr auto value = Value;
    static constexpr auto name() -> ref<str> { return text<Name>(); }
    static constexpr auto matches(ref<str> input, Case matching) -> bool {
        return equal(input, name(), matching) || (equal(input, text<Aliases>(), matching) || ...);
    }
};

template<typename T, str_::fixed_string Name, typename... Entries>
struct EnumFormat {
    static constexpr auto name(T value) -> Option<ref<str>> {
        Option<ref<str>> output;
        ((value == Entries::value ? (void)(output = Some(Entries::name())) : (void)0), ...);
        return output;
    }
    static auto fmt(T value, fmt::Formatter& formatter) -> bool {
        auto known = name(value);
        if (known.is_some()) return formatter.pad(*known);
        if constexpr (__is_signed(__underlying_type(T)))
            return formatter.write_fmt(
                fmt::Arguments::make("{}({})", text<Name>(), i128(static_cast<__int128>(value))));
        else
            return formatter.write_fmt(fmt::Arguments::make(
                "{}({})", text<Name>(), u128(static_cast<unsigned __int128>(value))));
    }
};

struct ParseEnumError {};

template<typename T, str_::fixed_string Name, T... Values>
struct EnumValues {
    static constexpr array<T, sizeof...(Values)> values { Values... };
    static constexpr auto                        name() -> ref<str> { return text<Name>(); }
    static auto iter() { return array<T, sizeof...(Values)> { Values... }.into_iter(); }
};

template<typename T, typename I>
struct TryFromEnumError {
    I value;
};

template<typename T, bool Insensitive, typename... Entries>
struct EnumFromStr {
    using Err = ParseEnumError;
    static constexpr auto parse(ref<str> input,
                                Case matching = Insensitive ? Case::AsciiInsensitive
                                                            : Case::Sensitive) -> Result<T, Err> {
        Option<T> output;
        ((Entries::matches(input, matching) ? (void)(output = Some(T(Entries::value))) : (void)0),
         ...);
        if (output.is_some()) return Ok(*output);
        return rstd::Err(ParseEnumError {});
    }
    static constexpr auto from_str(ref<str> input) -> Result<T, Err> { return parse(input); }
};

template<typename T, typename Member>
struct TransparentFromStr {
    using Value = mtp::rm_cvf<decltype(Member::get(declval<const T&>()))>;
    using Err   = typename Impl<str_::FromStr, Value>::Err;
    static auto from_str(ref<str> input) -> Result<T, Err> {
        auto value = rstd::from_str<Value>(input);
        if (value.is_err()) return rstd::Err(rstd::move(value).unwrap_err());
        return Ok(T { rstd::move(value).unwrap() });
    }
};

template<typename T>
concept DerivedDebug = requires { rstd_debug_definition(static_cast<T*>(nullptr)); };
template<typename T>
concept DerivedDisplay = requires { rstd_display_definition(static_cast<T*>(nullptr)); };
template<typename T>
concept DerivedFromStr = requires { rstd_from_str_definition(static_cast<T*>(nullptr)); };
template<typename T>
concept DerivedEnumIter =
    __is_enum(T) && requires { rstd_enum_iter_definition(static_cast<T*>(nullptr)); };

template<DerivedDebug T>
using DebugDefinition = decltype(rstd_debug_definition(static_cast<T*>(nullptr)));
template<DerivedDisplay T>
using DisplayDefinition = decltype(rstd_display_definition(static_cast<T*>(nullptr)));
template<DerivedFromStr T>
using FromStrDefinition = decltype(rstd_from_str_definition(static_cast<T*>(nullptr)));
template<DerivedEnumIter T>
using EnumIterDefinition = decltype(rstd_enum_iter_definition(static_cast<T*>(nullptr)));
template<convert::DerivedEnumTryFrom T>
using EnumTryFromDefinition = decltype(rstd_enum_try_from_definition(static_cast<T*>(nullptr)));

template<DerivedFromStr T>
    requires(__is_enum(T))
constexpr auto parse_enum(ref<str> input, Case matching) -> Result<T, ParseEnumError> {
    return FromStrDefinition<T>::parse(input, matching);
}
} // namespace rstd::derive

export namespace rstd
{
template<derive::DerivedEnumIter T>
struct Impl<EnumIter, T> {
    static auto iter() { return derive::EnumIterDefinition<T>::iter(); }
};

template<typename I, convert::DerivedEnumTryFrom T>
    requires(num::Integer<I> || num::PrimitiveInteger<I>)
struct Impl<convert::TryFrom<I>, T> {
    using Error = derive::TryFromEnumError<T, I>;
    static auto try_from(I value) -> Result<T, Error> {
        auto integer = rstd::try_into<__underlying_type(T)>(value);
        if (integer.is_err()) return Err(Error { value });
        for (auto candidate : derive::EnumTryFromDefinition<T>::values)
            if (static_cast<__underlying_type(T)>(candidate) == *integer) return Ok(candidate);
        return Err(Error { value });
    }
};

template<typename T, typename I>
struct Impl<fmt::Display, derive::TryFromEnumError<T, I>>
    : ImplBase<derive::TryFromEnumError<T, I>> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_fmt(fmt::Arguments::make(
            "Invalid {}: {}", derive::EnumTryFromDefinition<T>::name(), this->self().value));
    }
};
template<typename T, typename I>
struct Impl<fmt::Debug, derive::TryFromEnumError<T, I>>
    : Impl<fmt::Display, derive::TryFromEnumError<T, I>> {};
template<typename T, typename I>
struct Impl<error::Error, derive::TryFromEnumError<T, I>>
    : DefaultInImpl<error::Error, derive::TryFromEnumError<T, I>> {};

template<derive::DerivedDebug T>
struct Impl<fmt::Debug, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return derive::DebugDefinition<T>::fmt(this->self(), formatter);
    }
};
template<derive::DerivedDisplay T>
struct Impl<fmt::Display, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return derive::DisplayDefinition<T>::fmt(this->self(), formatter);
    }
};
template<derive::DerivedFromStr T>
struct Impl<str_::FromStr, T> {
    using Err = typename derive::FromStrDefinition<T>::Err;
    static auto from_str(ref<str> input) -> Result<T, Err> {
        return derive::FromStrDefinition<T>::from_str(input);
    }
};
template<>
struct Impl<fmt::Display, derive::ParseEnumError> : ImplBase<derive::ParseEnumError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_str("matching enum variant not found"_str);
    }
};
template<>
struct Impl<fmt::Debug, derive::ParseEnumError> : ImplBase<derive::ParseEnumError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_str("ParseEnumError"_str);
    }
};
template<>
struct Impl<error::Error, derive::ParseEnumError>
    : DefaultInImpl<error::Error, derive::ParseEnumError> {};
} // namespace rstd

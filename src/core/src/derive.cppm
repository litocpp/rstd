export module rstd.core:derive;
export import :fmt.debug;
export import :str.traits;
import :error;
import :fmt.integer;
export import :enum_;
export import :clone;
import :array;
import :num.convert;
import :convert.base;
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

template<typename T, auto Construct, typename... Fields>
struct CloneRecord {
    static_assert((Impled<mtp::rm_cvf<decltype(Fields::get(declval<const T&>()))>, clone::Clone> &&
                   ...),
                  "Clone fields must implement rstd::clone::Clone");

    static constexpr auto clone(const T& value) -> T {
        return Construct(as<clone::Clone>(Fields::get(value)).clone()...);
    }
};

template<typename T>
struct ClonePayload {
    template<size_t... I>
    static constexpr auto clone_fields(const T& value, mtp::index_sequence<I...>) -> T {
        using Definition = PayloadDefinition<T>;
        static_assert(
            (Impled<typename Definition::template field_at<I>::type, clone::Clone> && ...),
            "Clone fields must implement rstd::clone::Clone");
        return Definition::make(
            as<clone::Clone>(Definition::template field_at<I>::get(value)).clone()...);
    }
    static constexpr auto clone(const T& value) -> T {
        static_assert(DescribedPayload<T>, "Clone payloads require typed payload metadata");
        return clone_fields(value, mtp::make_index_sequence<PayloadDefinition<T>::field_count> {});
    }
};

template<typename T>
struct CloneVariants {
    static_assert(Impled<T, VariantDescription>, "Clone variants require rstd::VariantDescription");

    static constexpr auto clone(const T& value) -> T {
        Option<T> output;
        Impl<VariantDescription, T>::variants([&]<typename Entry>() {
            if constexpr (mtp::same_as<typename Entry::payload_type, void>) {
                if (Entry::matches(value)) output = Some(Entry::make());
            } else {
                static_assert(Impled<typename Entry::payload_type, clone::Clone>,
                              "Clone variant payloads must implement rstd::clone::Clone");
                if (Entry::matches(value))
                    output = Some(Entry::make(as<clone::Clone>(Entry::get(value)).clone()));
            }
        });
        return rstd::move(output).unwrap();
    }
};

template<typename T>
struct CloneEnum {
    static_assert(__is_enum(T));
    static constexpr auto clone(T value) -> T { return value; }
};

template<typename T, str_::fixed_string Name, typename... Fields>
struct DebugRecord {
    static auto fmt(const T& value, fmt::Formatter& formatter) -> bool {
        return fmt_as(value, formatter, text<Name>());
    }
    static auto fmt_as(const T& value, fmt::Formatter& formatter, ref<str> name) -> bool {
        fmt::DebugStruct output(formatter, name);
        (output.field(Fields::name(), Fields::get(value)), ...);
        return output.finish();
    }
};

template<typename T, str_::fixed_string Name, typename... Fields>
struct DebugTuple {
    static auto fmt(const T& value, fmt::Formatter& formatter) -> bool {
        return fmt_as(value, formatter, text<Name>());
    }
    static auto fmt_as(const T& value, fmt::Formatter& formatter, ref<str> name) -> bool {
        if constexpr (sizeof...(Fields) == 0)
            return formatter.write_str(name) && formatter.write_str("()"_str);
        else {
            fmt::DebugTuple output(formatter, name);
            (output.field(Fields::get(value)), ...);
            return output.finish();
        }
    }
};

template<typename T>
struct DebugPayload {
    static auto fmt(const T& value, fmt::Formatter& formatter) -> bool {
        return fmt_as(value, formatter, PayloadDefinition<T>::name());
    }
    static auto fmt_as(const T& value, fmt::Formatter& formatter, ref<str> name) -> bool {
        static_assert(DescribedPayload<T>, "Debug payloads require typed payload metadata");
        using Definition = PayloadDefinition<T>;
        if constexpr (Definition::kind == VariantKind::Unit)
            return formatter.write_str(name);
        else if constexpr (Definition::kind == VariantKind::Tuple) {
            if constexpr (Definition::field_count == 0)
                return formatter.write_str(name) && formatter.write_str("()"_str);
            else {
                fmt::DebugTuple output(formatter, name);
                Definition::fields([&]<typename Field>() {
                    output.field(Field::get(value));
                });
                return output.finish();
            }
        } else {
            static_assert(Definition::kind == VariantKind::Record,
                          "Debug payloads require unit, tuple or record metadata");
            fmt::DebugStruct output(formatter, name);
            Definition::fields([&]<typename Field>() {
                output.field(Field::name(), Field::get(value));
            });
            return output.finish();
        }
    }
};

template<typename T>
concept DerivedDebug =
    (! mtp::trait_default_tag<T>) && requires { rstd_debug_definition(static_cast<T*>(nullptr)); };
template<DerivedDebug T>
using DebugDefinition = decltype(rstd_debug_definition(static_cast<T*>(nullptr)));

template<str_::fixed_string Case, str_::fixed_string Name>
struct DebugVariantName {
    static constexpr auto name() -> ref<str> { return text<Name>(); }
    template<str_::fixed_string Target>
    static consteval auto matches() -> bool {
        if constexpr (Case.size() != Target.size())
            return false;
        else {
            for (size_t i = 0; i < Case.size(); ++i)
                if (Case.data[i] != Target.data[i]) return false;
            return true;
        }
    }
};

template<typename T, typename... Names>
struct DebugVariants {
    static_assert(Impled<T, VariantDescription>, "Debug variants require rstd::VariantDescription");
    static consteval auto valid_names() -> bool {
        bool valid = true;
        Impl<VariantDescription, T>::variants([&]<typename Entry>() {
            valid =
                valid && (size_t(0) + ... + size_t(Names::template matches<Entry::name>())) <= 1;
        });
        const auto known = []<typename Name>() {
            size_t found = 0;
            Impl<VariantDescription, T>::variants([&]<typename Entry>() {
                if constexpr (Name::template matches<Entry::name>()) ++found;
            });
            return found == 1;
        };
        return valid && (known.template operator()<Names>() && ...);
    }
    static_assert(valid_names(), "Debug variant name overrides must be known and unique");
    template<typename Entry>
    static constexpr auto variant_name() -> ref<str> {
        auto result = text<Entry::name>();
        (([&] {
             if constexpr (Names::template matches<Entry::name>()) result = Names::name();
         }()),
         ...);
        return result;
    }
    static auto fmt(const T& value, fmt::Formatter& formatter) -> bool {
        bool result = false;
        Impl<VariantDescription, T>::variants([&]<typename Entry>() {
            if (! Entry::matches(value)) return;
            auto name = variant_name<Entry>();
            if constexpr (mtp::same_as<typename Entry::payload_type, void>) {
                result = formatter.write_str(name);
            } else {
                const auto& payload = Entry::get(value);
                using Payload       = typename Entry::payload_type;
                if constexpr (DerivedDebug<Payload> && requires {
                                  DebugDefinition<Payload>::fmt_as(payload, formatter, name);
                              })
                    result = DebugDefinition<Payload>::fmt_as(payload, formatter, name);
                else {
                    static_assert(Impled<Payload, fmt::Debug>,
                                  "Debug variant payloads must implement rstd::fmt::Debug");
                    result = formatter.write_str(name) &&
                             formatter.write_fmt(fmt::Arguments::make("({:?})", payload));
                }
            }
        });
        return result;
    }
};

template<typename T, str_::fixed_string Format, typename... Fields>
struct DisplayRecord {
    static auto fmt(const T& value, fmt::Formatter& formatter) -> bool {
        if constexpr (sizeof...(Fields) == 0) {
            constexpr fmt::format_string<> checked(Format.data);
            return formatter.write_fmt(
                fmt::Arguments { checked.data(), checked.size(), nullptr, 0 });
        } else
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

template<str_::fixed_string Name, str_::fixed_string Format>
struct UnitFormat {
    static constexpr auto name() -> ref<str> { return text<Name>(); }
    static auto           fmt(fmt::Formatter& formatter) -> bool {
        constexpr fmt::format_string<> checked(Format.data);
        return formatter.write_fmt(fmt::Arguments { checked.data(), checked.size(), nullptr, 0 });
    }
};

template<typename T, typename... Units>
struct DisplayVariants {
    static_assert(Impled<T, VariantDescription>,
                  "Display variants require rstd::VariantDescription");
    template<typename Entry>
    static constexpr auto unit_count() -> size_t {
        return (size_t(0) + ... +
                size_t(equal(text<Entry::name>(), Units::name(), Case::Sensitive)));
    }
    static constexpr auto valid_units() -> bool {
        bool valid = true;
        Impl<VariantDescription, T>::variants([&]<typename Entry>() {
            if constexpr (mtp::same_as<typename Entry::payload_type, void>)
                valid = valid && unit_count<Entry>() == 1;
        });
        const auto known = []<typename Unit>() {
            bool found = false;
            Impl<VariantDescription, T>::variants([&]<typename Entry>() {
                if constexpr (mtp::same_as<typename Entry::payload_type, void>)
                    found = found || equal(text<Entry::name>(), Unit::name(), Case::Sensitive);
            });
            return found;
        };
        return valid && (known.template operator()<Units>() && ...);
    }
    static_assert(
        valid_units(),
        "Display variants require exactly one unit format per unit variant and no unknown units");
    static auto fmt(const T& value, fmt::Formatter& formatter) -> bool {
        bool result = false;
        Impl<VariantDescription, T>::variants([&]<typename Entry>() {
            if (! Entry::matches(value)) return;
            if constexpr (mtp::same_as<typename Entry::payload_type, void>) {
                (
                    [&] {
                        if (equal(text<Entry::name>(), Units::name(), Case::Sensitive))
                            result = Units::fmt(formatter);
                    }(),
                    ...);
            } else {
                static_assert(Impled<typename Entry::payload_type, fmt::Display>,
                              "Display variant payloads must implement rstd::fmt::Display");
                result = as<fmt::Display>(Entry::get(value)).fmt(formatter);
            }
        });
        return result;
    }
};

inline auto borrow_error(error::ErrorRef value) noexcept -> error::ErrorRef {
    return value;
}
template<typename T>
auto borrow_error(ref<T> value) noexcept -> error::ErrorRef {
    return dyn<error::Error>::from_ref(*value);
}
template<typename T>
auto borrow_error(const T& value [[clang::lifetimebound]]) noexcept -> error::ErrorRef {
    if constexpr (mtp::same_as<T, error::ErrorRef>)
        return value;
    else if constexpr (requires { value.deref(); })
        return borrow_error(value.deref());
    else {
        static_assert(
            Impled<T, error::Error>,
            "Error source fields must implement rstd::error::Error or dereference to an error");
        return dyn<error::Error>::from_ref(value);
    }
}

template<typename T>
struct ErrorSource {
    static auto get(const T& value [[clang::lifetimebound]]) noexcept -> Option<error::ErrorRef> {
        return Some(borrow_error(value));
    }
};
template<typename T>
struct ErrorSource<Option<T>> {
    static auto get(const Option<T>& value [[clang::lifetimebound]]) noexcept
        -> Option<error::ErrorRef> {
        if (value.is_none()) return None();
        return Some(borrow_error(*value));
    }
};

template<typename T, typename Member, bool Enabled>
struct ErrorFromType {
    using type = void;
};
template<typename T, typename Member>
struct ErrorFromType<T, Member, true> {
    using type = mtp::rm_cvf<decltype(Member::get(declval<const T&>()))>;
};

template<typename T, bool Transparent, typename Member = void, bool From = false>
struct ErrorRecord {
    using FromType = typename ErrorFromType<T, Member, From>::type;
    static auto source(const T& value [[clang::lifetimebound]]) noexcept
        -> Option<error::ErrorRef> {
        if constexpr (mtp::same_as<Member, void>)
            return None();
        else {
            const auto& field    = Member::get(value);
            auto        borrowed = ErrorSource<mtp::rm_cvf<decltype(field)>>::get(field);
            if constexpr (Transparent) {
                if (borrowed.is_none()) return None();
                return (*borrowed)->source();
            } else
                return borrowed;
        }
    }
    template<typename S>
    static constexpr auto accepts_from() -> bool {
        if constexpr (From)
            return mtp::same_as<S, FromType>;
        else
            return false;
    }
    template<typename S>
        requires(accepts_from<S>())
    static auto from(S value) -> T {
        return T { rstd::move(value) };
    }
};

template<typename T>
concept DerivedError =
    (! mtp::trait_default_tag<T>) && requires { rstd_error_definition(static_cast<T*>(nullptr)); };
template<DerivedError T>
using ErrorDefinition = decltype(rstd_error_definition(static_cast<T*>(nullptr)));

template<typename T, typename S>
constexpr auto accepts_error_from() -> bool {
    if constexpr (DerivedError<T>)
        return ErrorDefinition<T>::template accepts_from<S>();
    else
        return false;
}

template<typename T, typename... Units>
struct ErrorVariants : DisplayVariants<T, Units...> {
    using FromType = void;
    template<typename S>
    static constexpr auto from_count() -> size_t {
        size_t count = 0;
        Impl<VariantDescription, T>::variants([&]<typename Entry>() {
            if constexpr (! mtp::same_as<typename Entry::payload_type, void>)
                if constexpr (accepts_error_from<typename Entry::payload_type, S>()) ++count;
        });
        return count;
    }
    static constexpr auto unique_from() -> bool {
        bool unique = true;
        Impl<VariantDescription, T>::variants([&]<typename Entry>() {
            if constexpr (! mtp::same_as<typename Entry::payload_type, void>) {
                using Payload = typename Entry::payload_type;
                if constexpr (DerivedError<Payload>) {
                    using Source = typename ErrorDefinition<Payload>::FromType;
                    if constexpr (! mtp::same_as<Source, void>)
                        unique = unique && from_count<Source>() <= 1;
                }
            }
        });
        return unique;
    }
    static_assert(unique_from(), "Error variants have duplicate From source types");
    template<typename S>
    static constexpr auto accepts_from() -> bool {
        static_assert(from_count<S>() <= 1, "Error variants have duplicate From source types");
        return from_count<S>() == 1;
    }
    template<typename S>
        requires(accepts_from<S>())
    static auto from(S value) -> T {
        Option<T> result;
        Impl<VariantDescription, T>::variants([&]<typename Entry>() {
            if constexpr (! mtp::same_as<typename Entry::payload_type, void>) {
                using Payload = typename Entry::payload_type;
                if constexpr (accepts_error_from<Payload, S>())
                    result = Some(Entry::make(ErrorDefinition<Payload>::from(rstd::move(value))));
            }
        });
        return rstd::move(result).unwrap();
    }
    static auto source(const T& value [[clang::lifetimebound]]) noexcept
        -> Option<error::ErrorRef> {
        Option<error::ErrorRef> result;
        Impl<VariantDescription, T>::variants([&]<typename Entry>() {
            if constexpr (! mtp::same_as<typename Entry::payload_type, void>) {
                if (Entry::matches(value)) result = as<error::Error>(Entry::get(value)).source();
            }
        });
        return result;
    }
};

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
concept DerivedDisplay = (! mtp::trait_default_tag<T>) &&
                         requires { rstd_display_definition(static_cast<T*>(nullptr)); };
template<typename T>
concept DerivedFromStr = (! mtp::trait_default_tag<T>) &&
                         requires { rstd_from_str_definition(static_cast<T*>(nullptr)); };
template<typename T>
concept DerivedEnumIter =
    __is_enum(T) && requires { rstd_enum_iter_definition(static_cast<T*>(nullptr)); };

template<DerivedClone T>
using CloneDefinition = decltype(rstd_clone_definition(static_cast<T*>(nullptr)));
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
template<derive::DerivedError T>
struct Impl<error::Error, T> : DefaultInImpl<error::Error, T> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        return derive::ErrorDefinition<T>::source(this->self());
    }
};

template<typename S, derive::DerivedError T>
    requires(derive::accepts_error_from<T, S>())
struct Impl<convert::From<S>, T> {
    static auto from(S value) -> T { return derive::ErrorDefinition<T>::from(rstd::move(value)); }
};

template<derive::DerivedClone T>
struct Impl<clone::Clone, T> : DefaultInImpl<clone::Clone, T> {
    constexpr auto clone() const -> T { return derive::CloneDefinition<T>::clone(this->self()); }
};

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

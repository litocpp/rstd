export module rstd.serde:derive;
export import :record;

using namespace rstd::prelude;
using namespace rstd::literals;

export namespace rstd::serde
{

template<size_t N>
struct Text {
    char value[N];
    constexpr Text(const char (&input)[N]) {
        for (size_t i = 0; i < N; ++i) value[i] = input[i];
    }
    constexpr auto size() const -> size_t { return N - 1; }
    constexpr auto data() const -> const char* { return value; }
    auto           view() const -> ref<str> {
        return ref<str>::from_raw_parts_unchecked(reinterpret_cast<const byte*>(value),
                                                  usize(N - 1));
    }
};

enum class FieldPolicy
{
    Required,
    Defaulted,
    Skip
};
enum class Direction
{
    Both,
    Serialize,
    Deserialize
};
struct Describe final {};

template<typename T>
concept Describable = mtp::complete<Impl<Describe, mtp::rm_cvf<T>>>;

template<typename T, typename Sink>
constexpr void describe(Sink& sink) {
    static_assert(Describable<T>, "serde type requires an explicit Impl<Describe, T>");
    Impl<Describe, mtp::rm_cvf<T>>::describe(sink);
}

template<typename T>
struct MemberValue;
template<typename T, typename V>
struct MemberValue<V T::*> {
    using type = V;
};

template<auto Pointer, Text Name, FieldPolicy Policy = FieldPolicy::Required, Text... Aliases>
struct Member {
    using type                    = typename MemberValue<decltype(Pointer)>::type;
    static constexpr auto pointer = Pointer;
    static constexpr auto name    = Name;
    static constexpr auto policy  = Policy;

    template<typename F>
    static constexpr void aliases(F function) {
        (function(Aliases), ...);
    }

    template<typename T>
    static auto slot(T& value) {
        const ref<str> names[] = { Aliases.view()..., ref<str> {} };
        auto           aliases = slice<ref<str>>::from_raw_parts(names, usize(sizeof...(Aliases)));
        if constexpr (Policy == FieldPolicy::Defaulted)
            return DefaultedField<type>(Name.view(), aliases, rstd::move(value.*Pointer));
        else
            return RequiredField<type>(Name.view(), aliases);
    }
};

template<typename T, Text Name, Direction Mode, UnknownFieldPolicy Unknown, typename... Members>
struct RecordDefinition {
    static constexpr auto name           = Name;
    static constexpr bool serializable   = Mode != Direction::Deserialize;
    static constexpr bool deserializable = Mode != Direction::Serialize;
    static constexpr auto unknown_fields = Unknown;

    template<typename Function>
    static constexpr void fields(Function function) {
        (function.template operator()<Members>(), ...);
    }

    template<typename Serializer>
    static auto serialize(Serializer& serializer, const T& value) ->
        typename Serializer::result_type {
        auto started =
            serializer.begin_map(usize((size_t {} + ... + (Members::policy != FieldPolicy::Skip))));
        if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
        auto map    = rstd::move(started).unwrap_unchecked();
        auto result = Result<empty, typename Serializer::error_type>(Ok(empty {}));
        auto write  = [&]<typename Field>() {
            if constexpr (Field::policy != FieldPolicy::Skip) {
                if (result.is_ok()) result = field(map, Field::name.view(), value.*Field::pointer);
            }
        };
        fields(write);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
        return map.end();
    }

    template<size_t I, typename Deserializer, typename... Slots>
    static auto read(Deserializer& deserializer, T& value, Slots&... slots)
        -> Result<empty, typename Deserializer::error_type> {
        if constexpr (I == sizeof...(Members)) {
            if constexpr (sizeof...(Slots) != 0)
                return deserialize_record(deserializer, Unknown, slots...);
            else
                return deserialize_record(deserializer,
                                          [&](ref<str> name, auto& map)
                                              -> Result<empty, typename Deserializer::error_type> {
                                              if constexpr (Unknown == UnknownFieldPolicy::Reject)
                                                  return Err(deserializer.unknown_field(name));
                                              else
                                                  return map.ignore_value();
                                          });
        } else {
            using Field = mtp::tuple_element<I, tuple<Members...>>;
            if constexpr (Field::policy == FieldPolicy::Skip) {
                return read<I + 1>(deserializer, value, slots...);
            } else {
                static_assert(__is_assignable(typename Field::type&, typename Field::type&&),
                              "serde derived fields must be move assignable");
                auto slot   = Field::slot(value);
                auto result = read<I + 1>(deserializer, value, slots..., slot);
                if (result.is_err()) return result;
                if constexpr (Field::policy == FieldPolicy::Defaulted) {
                    value.*Field::pointer = slot.take();
                } else {
                    auto decoded = slot.take(deserializer);
                    if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err_unchecked());
                    value.*Field::pointer = rstd::move(decoded).unwrap_unchecked();
                }
                return Ok(empty {});
            }
        }
    }

    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<T, typename Deserializer::error_type> {
        static_assert(__is_aggregate(T), "serde derive requires an aggregate");
        auto value  = T {};
        auto result = read<0>(deserializer, value);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
        return Ok(rstd::move(value));
    }

    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template record<RecordDefinition>();
    }
};

template<auto Value, Text Name>
struct Variant {
    static constexpr auto value = Value;
    static constexpr auto name  = Name;
};

template<typename T, Text Name, Direction Mode, typename... Variants>
struct EnumDefinition {
    static consteval auto unique_values() -> bool {
        const T values[] = { Variants::value..., T {} };
        for (size_t i = 0; i < sizeof...(Variants); ++i)
            for (size_t j = 0; j < i; ++j)
                if (values[i] == values[j]) return false;
        return sizeof...(Variants) > 0;
    }
    static_assert(unique_values(), "serde enum values must be nonempty and unique");
    static constexpr auto name           = Name;
    static constexpr bool serializable   = Mode != Direction::Deserialize;
    static constexpr bool deserializable = Mode != Direction::Serialize;

    template<typename Function>
    static constexpr void variants(Function function) {
        (function.template operator()<Variants>(), ...);
    }

    template<typename Serializer>
    static auto serialize(Serializer& serializer, T value) -> typename Serializer::result_type {
        auto result = typename Serializer::result_type(
            Err(serializer.invalid_value("unknown enum value"_str)));
        auto matched = false;
        variants([&]<typename V>() {
            if (! matched && value == V::value) {
                matched = true;
                result  = serializer.serialize_unit_variant(V::name.view());
            }
        });
        return result;
    }

    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<T, typename Deserializer::error_type> {
        auto started = deserializer.begin_enum();
        if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
        auto input  = rstd::move(started).unwrap_unchecked();
        auto result = Result<T, typename Deserializer::error_type>(
            Err(deserializer.unknown_variant(input.variant())));
        variants([&]<typename V>() {
            if (input.variant() == V::name.view()) {
                auto checked = input.unit();
                if (checked.is_err())
                    result = Err(rstd::move(checked).unwrap_err_unchecked());
                else
                    result = Ok(V::value);
            }
        });
        return result;
    }

    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template enumeration<EnumDefinition>();
    }
};

template<typename T>
concept Derived = requires { rstd_serde_definition(static_cast<T*>(nullptr)); };

template<Derived T>
using Definition = decltype(rstd_serde_definition(static_cast<T*>(nullptr)));

} // namespace rstd::serde

namespace rstd
{

template<serde::Derived T>
    requires serde::Definition<T>::serializable
struct Impl<serde::Serialize, T> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const T& value) ->
        typename Serializer::result_type {
        return serde::Definition<T>::serialize(serializer, value);
    }
};
template<serde::Derived T>
    requires serde::Definition<T>::deserializable
struct Impl<serde::Deserialize, T> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<T, typename Deserializer::error_type> {
        return serde::Definition<T>::deserialize(deserializer);
    }
};
template<serde::Derived T>
struct Impl<serde::Describe, T> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        serde::Definition<T>::describe(sink);
    }
};
template<>
struct Impl<serde::Describe, bool> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.boolean();
    }
};
template<num::SignedInteger T>
    requires(sizeof(T) <= sizeof(i64))
struct Impl<serde::Describe, T> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.signed_integer(sizeof(T) * 8);
    }
};
template<num::UnsignedInteger T>
    requires(sizeof(T) <= sizeof(u64))
struct Impl<serde::Describe, T> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.unsigned_integer(sizeof(T) * 8);
    }
};
template<num::Float T>
struct Impl<serde::Describe, T> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.floating(sizeof(T) * 8);
    }
};
template<>
struct Impl<serde::Describe, ::alloc::string::String> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.string();
    }
};
template<typename T>
    requires serde::Describable<T>
struct Impl<serde::Describe, Option<T>> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template optional<T>();
    }
};
template<typename T>
    requires serde::Describable<T>
struct Impl<serde::Describe, ::alloc::vec::Vec<T>> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template sequence<T>();
    }
};

} // namespace rstd

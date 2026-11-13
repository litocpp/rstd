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
    static auto deserialize_with_defaults(Deserializer& deserializer, T value)
        -> Result<T, typename Deserializer::error_type> {
        auto result = read<0>(deserializer, value);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
        return Ok(rstd::move(value));
    }

    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<T, typename Deserializer::error_type> {
        static_assert(__is_aggregate(T), "serde derive requires an aggregate");
        return deserialize_with_defaults(deserializer, T {});
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

template<typename T, Text Name, Direction Mode, typename... Variants>
struct StringEnumDefinition : EnumDefinition<T, Name, Mode, Variants...> {
    template<typename S>
    static auto serialize(S& sink, T value) -> typename S::result_type {
        auto result = typename S::result_type(Err(sink.invalid_value("unknown enum value"_str)));
        StringEnumDefinition::variants([&]<typename V>() {
            if (value == V::value) result = sink.serialize_string(V::name.view());
        });
        return result;
    }
    template<typename D>
    static auto deserialize(D& source) -> Result<T, typename D::error_type> {
        auto name = source.deserialize_string();
        if (name.is_err()) return Err(rstd::move(name).unwrap_err_unchecked());
        auto result =
            Result<T, typename D::error_type>(Err(source.unknown_variant(name->as_str())));
        StringEnumDefinition::variants([&]<typename V>() {
            if (name->as_str() == V::name.view()) result = Ok(V::value);
        });
        return result;
    }
};

template<typename T, Text Name, Direction Mode>
struct VariantDefinition {
    static_assert(mtp::complete<Impl<VariantDescription, T>>,
                  "serde variants requires an explicit VariantDescription");
    static constexpr auto name           = Name;
    static constexpr bool serializable   = Mode != Direction::Deserialize;
    static constexpr bool deserializable = Mode != Direction::Serialize;
    template<typename F>
    static constexpr void variants(F function) {
        Impl<VariantDescription, T>::variants(function);
    }
    template<typename S>
    static auto serialize(S& sink, const T& value) -> typename S::result_type {
        auto result = typename S::result_type(Err(sink.invalid_value("Unknown variant"_str)));
        variants([&]<typename V>() {
            if (V::matches(value)) {
                if constexpr (__is_void(typename V::payload_type))
                    result = sink.serialize_unit_variant(Text(V::name).view());
                else
                    result = sink.serialize_newtype_variant(Text(V::name).view(), V::get(value));
            }
        });
        return result;
    }
    template<typename D>
    static auto deserialize(D& source) -> Result<T, typename D::error_type> {
        auto started = source.begin_enum();
        if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
        auto input = rstd::move(started).unwrap_unchecked();
        auto result =
            Result<T, typename D::error_type>(Err(source.unknown_variant(input.variant())));
        variants([&]<typename V>() {
            if (input.variant() == Text(V::name).view()) {
                if constexpr (__is_void(typename V::payload_type)) {
                    auto checked = input.unit();
                    if (checked.is_err())
                        result = Err(rstd::move(checked).unwrap_err_unchecked());
                    else
                        result = Ok(V::make());
                } else {
                    auto value = input.template value<typename V::payload_type>();
                    if (value.is_err())
                        result = Err(rstd::move(value).unwrap_err_unchecked());
                    else
                        result = Ok(V::make(rstd::move(value).unwrap_unchecked()));
                }
            }
        });
        return result;
    }
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template variants<VariantDefinition>();
    }
};

template<typename T, Text Name, Direction Mode, auto Pointer>
struct NewtypeDefinition {
    static constexpr auto name           = Name;
    static constexpr bool serializable   = Mode != Direction::Deserialize;
    static constexpr bool deserializable = Mode != Direction::Serialize;
    using type                           = typename MemberValue<decltype(Pointer)>::type;

    template<typename S>
    static auto serialize(S& sink, const T& value) -> typename S::result_type {
        return sink.serialize_newtype(Name.view(), value.*Pointer);
    }
    template<typename D>
    static auto deserialize(D& source) -> Result<T, typename D::error_type> {
        auto value = source.template deserialize_newtype<type>(Name.view());
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(T { rstd::move(value).unwrap_unchecked() });
    }
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        rstd::serde::describe<type>(sink);
    }
};

template<typename T,
         Text               Name,
         Direction          Mode,
         Text               Tag,
         Text               Content,
         UnknownFieldPolicy Unknown = UnknownFieldPolicy::Reject>
struct AdjacentVariantDefinition : VariantDefinition<T, Name, Mode> {
    static constexpr auto tag            = Tag;
    static constexpr auto content        = Content;
    static constexpr auto unknown_fields = Unknown;
    template<typename S>
    static auto serialize(S& sink, const T& value) -> typename S::result_type {
        auto result = typename S::result_type(Err(sink.invalid_value("unknown variant"_str)));
        AdjacentVariantDefinition::variants([&]<typename V>() {
            if (! V::matches(value)) return;
            constexpr bool unit    = __is_void(typename V::payload_type);
            auto           started = sink.begin_map(usize(unit ? 1 : 2));
            if (started.is_err()) {
                result = Err(rstd::move(started).unwrap_err_unchecked());
                return;
            }
            auto map = rstd::move(started).unwrap_unchecked();
            auto tag = field(map, Tag.view(), Text(V::name).view());
            if (tag.is_err()) {
                result = Err(rstd::move(tag).unwrap_err_unchecked());
                return;
            }
            if constexpr (! unit) {
                auto content = field(map, Content.view(), V::get(value));
                if (content.is_err()) {
                    result = Err(rstd::move(content).unwrap_err_unchecked());
                    return;
                }
            }
            result = map.end();
        });
        return result;
    }
    template<typename D>
    static auto deserialize(D& source) -> Result<T, typename D::error_type> {
        auto started = source.begin_adjacent_enum(Tag.view(), Content.view(), Unknown);
        if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
        auto input = rstd::move(started).unwrap_unchecked();
        auto result =
            Result<T, typename D::error_type>(Err(source.unknown_variant(input.variant())));
        AdjacentVariantDefinition::variants([&]<typename V>() {
            if (input.variant() != Text(V::name).view()) return;
            if constexpr (__is_void(typename V::payload_type)) {
                auto checked = input.unit();
                if (checked.is_err())
                    result = Err(rstd::move(checked).unwrap_err_unchecked());
                else
                    result = Ok(V::make());
            } else {
                auto value = input.template value<typename V::payload_type>();
                if (value.is_err())
                    result = Err(rstd::move(value).unwrap_err_unchecked());
                else
                    result = Ok(V::make(rstd::move(value).unwrap_unchecked()));
            }
        });
        return result;
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

template<>
struct Impl<serde::Describe, empty> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.unit();
    }
};
template<typename K, typename V>
    requires serde::Describable<V>
struct Impl<serde::Describe, ::alloc::collections::BTreeMap<K, V>> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template map<K, V>();
    }
};
template<typename T, typename E>
    requires serde::Describable<T> && serde::Describable<E>
struct Impl<serde::Describe, Result<T, E>> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template result<T, E>();
    }
};
template<typename T>
    requires serde::Describable<T>
struct Impl<serde::Describe, serde::TaggedOption<T>> {
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template tagged_option<T>();
    }
};
} // namespace rstd

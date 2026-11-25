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
        const array<ref<str>, sizeof...(Aliases)> names { Aliases.view()... };
        auto                                      aliases = names.as_slice();
        if constexpr (Policy == FieldPolicy::Defaulted)
            return DefaultedField<type>(Name.view(), aliases, rstd::move(value.*Pointer));
        else
            return RequiredField<type>(Name.view(), aliases);
    }

    static auto unseeded_slot() {
        const array<ref<str>, sizeof...(Aliases)> names { Aliases.view()... };
        return OptionalField<type>(Name.view(), names.as_slice());
    }
};

template<auto Pointer, Text Name, auto Factory, Text... Aliases>
struct DefaultedMember : Member<Pointer, Name, FieldPolicy::Defaulted, Aliases...> {
    using type = typename MemberValue<decltype(Pointer)>::type;

    static auto default_value() -> type { return type(Factory()); }

    template<typename T>
    static auto slot(T&) {
        const array<ref<str>, sizeof...(Aliases)> names { Aliases.view()... };
        return DefaultFactoryField<type, Factory>(Name.view(), names.as_slice());
    }
};

template<typename Field>
struct DecodedMember {
    Option<typename Field::type> value;
};

template<typename... Members>
struct RecordValues : DecodedMember<Members>... {
    template<typename Field>
    auto field() -> Option<typename Field::type>& {
        return static_cast<DecodedMember<Field>&>(*this).value;
    }

    template<auto Pointer, typename First, typename... Rest>
    auto take_member() {
        if constexpr (mtp::same_as<mtp::rm_cvf<decltype(Pointer)>,
                                   mtp::rm_cvf<decltype(First::pointer)>>) {
            if constexpr (Pointer == First::pointer)
                return field<First>().take().unwrap_unchecked();
            else
                return take_member<Pointer, Rest...>();
        } else {
            return take_member<Pointer, Rest...>();
        }
    }

    template<auto Pointer>
    auto take() {
        return take_member<Pointer, Members...>();
    }
};

template<typename T, Text Name, Direction Mode, UnknownFieldPolicy Unknown, typename... Members>
struct RecordDefinition {
    using model_type                     = T;
    static constexpr auto name           = Name;
    static constexpr bool serializable   = Mode != Direction::Deserialize;
    static constexpr bool deserializable = Mode != Direction::Serialize;
    static constexpr auto unknown_fields = Unknown;

    using values_type = RecordValues<Members...>;

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

    template<size_t I, typename D>
    static auto take_fields(D&, T&) -> Result<empty, typename D::error_type> {
        return Ok(empty {});
    }

    template<bool Inherit, typename D, typename... Slots>
    static auto parse_slots(D& source, Slots&... slots) -> Result<empty, typename D::error_type> {
        if constexpr (sizeof...(Slots) != 0) {
            if constexpr (Inherit)
                return deserialize_record(source, Unknown, slots...);
            else
                return deserialize_record(source, DeclaredRecordPolicy { Unknown }, slots...);
        } else {
            return deserialize_record(
                source, [&](ref<str> name, auto& map) -> Result<empty, typename D::error_type> {
                    if ((Inherit ? record_unknown_fields(source, Unknown) : Unknown) ==
                        UnknownFieldPolicy::Reject)
                        return Err(source.unknown_field(name));
                    else
                        return map.ignore_value();
                });
        }
    }

    template<size_t I, typename D, typename... Slots>
    static auto collect(D& source, values_type& values, Slots&... slots)
        -> Result<empty, typename D::error_type> {
        if constexpr (I == sizeof...(Members)) {
            return Ok(empty {});
        } else {
            using Field = mtp::tuple_element<I, tuple<Members...>>;
            if constexpr (Field::policy == FieldPolicy::Skip) {
                return collect<I + 1>(source, values, slots...);
            } else {
                auto consume = [&]<typename First, typename... Rest>(
                                   First& first,
                                   Rest&... rest) -> Result<empty, typename D::error_type> {
                    auto& value = values.template field<Field>();
                    value       = first.take();
                    if constexpr (Field::policy == FieldPolicy::Required) {
                        if (value.is_none()) return Err(source.missing_field(Field::name.view()));
                    } else if constexpr (requires { Field::default_value(); }) {
                        if (value.is_none()) value = Some(Field::default_value());
                    }
                    return collect<I + 1>(source, values, rest...);
                };
                return consume(slots...);
            }
        }
    }

    template<size_t I, bool Inherit, typename D, typename... Slots>
    static auto read_constructed(D& source, values_type& values, Slots&... slots)
        -> Result<empty, typename D::error_type> {
        if constexpr (I == sizeof...(Members)) {
            auto parsed = parse_slots<Inherit>(source, slots...);
            if (parsed.is_err()) return parsed;
            return collect<0>(source, values, slots...);
        } else {
            using Field = mtp::tuple_element<I, tuple<Members...>>;
            if constexpr (Field::policy == FieldPolicy::Skip)
                return read_constructed<I + 1, Inherit>(source, values, slots...);
            else {
                auto slot = Field::unseeded_slot();
                return read_constructed<I + 1, Inherit>(source, values, slots..., slot);
            }
        }
    }

    template<bool Inherit, typename D, typename Construct>
    static auto deserialize_constructed(D& source, Construct construct)
        -> Result<T, typename D::error_type> {
        values_type values;
        auto        parsed = read_constructed<0, Inherit>(source, values);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err_unchecked());
        auto value = construct(values);
        fields([&]<typename Field>() {
            if constexpr (Field::policy == FieldPolicy::Defaulted) {
                auto& decoded = values.template field<Field>();
                if (decoded.is_some())
                    value.*Field::pointer = rstd::move(decoded).unwrap_unchecked();
            }
        });
        return Ok(rstd::move(value));
    }

    template<size_t I, typename D, typename First, typename... Rest>
    static auto take_fields(D& source, T& value, First& first, Rest&... rest)
        -> Result<empty, typename D::error_type> {
        using Field = mtp::tuple_element<I, tuple<Members...>>;
        if constexpr (Field::policy == FieldPolicy::Skip) {
            return take_fields<I + 1>(source, value, first, rest...);
        } else {
            if constexpr (Field::policy == FieldPolicy::Defaulted) {
                value.*Field::pointer = first.take();
            } else {
                auto decoded = first.take(source);
                if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err_unchecked());
                value.*Field::pointer = rstd::move(decoded).unwrap_unchecked();
            }
            return take_fields<I + 1>(source, value, rest...);
        }
    }

    template<size_t I, bool Inherit, typename Deserializer, typename... Slots>
    static auto read_with_policy(Deserializer& deserializer, T& value, Slots&... slots)
        -> Result<empty, typename Deserializer::error_type> {
        if constexpr (I == sizeof...(Members)) {
            auto result = parse_slots<Inherit>(deserializer, slots...);
            if (result.is_err()) return result;
            return take_fields<0>(deserializer, value, slots...);
        } else {
            using Field = mtp::tuple_element<I, tuple<Members...>>;
            if constexpr (Field::policy == FieldPolicy::Skip) {
                return read_with_policy<I + 1, Inherit>(deserializer, value, slots...);
            } else {
                static_assert(__is_assignable(typename Field::type&, typename Field::type&&),
                              "serde derived fields must be move assignable");
                auto slot = Field::slot(value);
                return read_with_policy<I + 1, Inherit>(deserializer, value, slots..., slot);
            }
        }
    }

    template<size_t I, typename Deserializer, typename... Slots>
    static auto read(Deserializer& deserializer, T& value, Slots&... slots)
        -> Result<empty, typename Deserializer::error_type> {
        return read_with_policy<I, true>(deserializer, value, slots...);
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

    template<typename Deserializer>
    static auto deserialize_declared(Deserializer& deserializer)
        -> Result<T, typename Deserializer::error_type> {
        static_assert(__is_aggregate(T), "serde derive requires an aggregate");
        T    value {};
        auto result = read_with_policy<0, false>(deserializer, value);
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
        return Ok(rstd::move(value));
    }

    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template record<RecordDefinition>();
    }
};

template<typename Base, auto Construct>
struct ConstructedRecordDefinition : Base {
    using model_type = typename Base::model_type;

    template<typename D>
    static auto deserialize(D& source) -> Result<model_type, typename D::error_type> {
        // Preserve existing initializer timing whenever a seed can be constructed.
        if constexpr (__is_constructible(model_type))
            return Base::deserialize(source);
        else
            return Base::template deserialize_constructed<true>(source, Construct);
    }

    template<typename D>
    static auto deserialize_declared(D& source) -> Result<model_type, typename D::error_type> {
        if constexpr (__is_constructible(model_type))
            return Base::deserialize_declared(source);
        else
            return Base::template deserialize_constructed<false>(source, Construct);
    }
};

template<typename Base, auto Validate>
struct ValidatedDefinition : Base {
    using model_type = typename Base::model_type;
    static_assert(
        requires(const model_type& value) {
            { Validate(value) } -> mtp::same_as<Result<empty, ref<str>>>;
        },
        "serde validator must return Result<empty, ref<str>> for const model_type&");

    template<typename D>
    static auto finish(D& source, Result<model_type, typename D::error_type> decoded)
        -> Result<model_type, typename D::error_type> {
        if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err_unchecked());
        auto value   = rstd::move(decoded).unwrap_unchecked();
        auto checked = Validate(static_cast<const model_type&>(value));
        if (checked.is_err()) return Err(source.invalid_value(checked.unwrap_err_unchecked()));
        return Ok(rstd::move(value));
    }

    template<typename D>
    static auto deserialize(D& source) -> Result<model_type, typename D::error_type> {
        return finish(source, Base::deserialize(source));
    }

    template<typename D>
    static auto deserialize_declared(D& source) -> Result<model_type, typename D::error_type> {
        return finish(source, Base::deserialize_declared(source));
    }

    template<typename D>
    static auto deserialize_with_defaults(D& source, model_type value)
        -> Result<model_type, typename D::error_type> {
        return finish(source, Base::deserialize_with_defaults(source, rstd::move(value)));
    }
};

template<typename Base, auto Validate>
using ValidatedRecordDefinition = ValidatedDefinition<Base, Validate>;

template<auto Value, Text Name>
struct Variant {
    static constexpr auto value = Value;
    static constexpr auto name  = Name;
};

template<Text Case, Text Name>
struct VariantName {
    static constexpr auto case_name = Case;
    static constexpr auto name      = Name;
};

template<typename L, typename R>
constexpr auto same_text(L left, R right) -> bool {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i)
        if (left.data()[i] != right.data()[i]) return false;
    return true;
}

template<typename Entry, typename... Names>
struct NamedVariant : Entry {
    static consteval auto wire_name() {
        if constexpr (sizeof...(Names) == 0)
            return Text(Entry::name);
        else
            return select_name<Names...>();
    }
    template<typename First, typename... Rest>
    static consteval auto select_name() {
        if constexpr (same_text(Text(Entry::name), First::case_name))
            return First::name;
        else if constexpr (sizeof...(Rest) > 0)
            return select_name<Rest...>();
        else
            return Text(Entry::name);
    }
    static constexpr auto name = wire_name();
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
        using Decoded = Result<T, typename Deserializer::error_type>;
        auto input    = rstd::move(started).unwrap_unchecked();
        auto result   = Option<Decoded>();
        variants([&]<typename V>() {
            if (input.variant() == V::name.view()) {
                auto checked = input.unit();
                if (checked.is_err())
                    result = Some(Decoded(Err(rstd::move(checked).unwrap_err_unchecked())));
                else
                    result = Some(Decoded(Ok(V::value)));
            }
        });
        if (result.is_some()) return rstd::move(result).unwrap_unchecked();
        return Err(deserializer.unknown_variant(input.variant()));
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
        auto result = Option<T>();
        StringEnumDefinition::variants([&]<typename V>() {
            if (name->as_str() == V::name.view()) result = Some(T(V::value));
        });
        if (result.is_some()) return Ok(rstd::move(result).unwrap_unchecked());
        return Err(source.unknown_variant(name->as_str()));
    }
};

template<typename T, Text Name, Direction Mode, typename... Names>
struct VariantDefinition {
    using model_type = T;
    static_assert(mtp::complete<Impl<VariantDescription, T>>,
                  "serde variants requires an explicit VariantDescription");
    static constexpr auto name           = Name;
    static constexpr bool serializable   = Mode != Direction::Deserialize;
    static constexpr bool deserializable = Mode != Direction::Serialize;
    template<Text Case>
    static consteval auto contains_case() -> bool {
        size_t count = 0;
        Impl<VariantDescription, T>::variants([&]<typename Entry>() {
            if (same_text(Text(Entry::name), Case)) ++count;
        });
        return count == 1;
    }
    static consteval auto valid_names() -> bool {
        bool       valid = true;
        const auto known = []<typename Mapping>() {
            return contains_case<Mapping::case_name>();
        };
        const auto unique = []<typename Mapping>() {
            return (size_t(0) + ... + size_t(same_text(Mapping::case_name, Names::case_name))) == 1;
        };
        valid =
            ((known.template operator()<Names>() && unique.template operator()<Names>()) && ...);
        Impl<VariantDescription, T>::variants([&]<typename First>() {
            size_t count = 0;
            Impl<VariantDescription, T>::variants([&]<typename Second>() {
                if (same_text(NamedVariant<First, Names...>::name,
                              NamedVariant<Second, Names...>::name))
                    ++count;
            });
            valid = valid && count == 1;
        });
        return valid;
    }
    static_assert(valid_names(), "serde variant names must be known and unique");
    template<typename F>
    static constexpr void variants(F function) {
        Impl<VariantDescription, T>::variants([&]<typename Entry>() {
            function.template operator()<NamedVariant<Entry, Names...>>();
        });
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
        return deserialize_variant(source, input, [&] {
            return source.unknown_variant(input.variant());
        });
    }
    template<typename D, EnumAccess Input, typename Unknown>
        requires requires(Unknown& unknown) {
            { unknown() } -> mtp::same_as<typename D::error_type>;
        }
    static auto deserialize_variant(D&, Input& input, Unknown unknown)
        -> Result<T, typename D::error_type> {
        using Decoded = Result<T, typename D::error_type>;
        auto result   = Option<Decoded>();
        variants([&]<typename V>() {
            if (input.variant() == Text(V::name).view()) {
                if constexpr (__is_void(typename V::payload_type)) {
                    auto checked = input.unit();
                    if (checked.is_err())
                        result = Some(Decoded(Err(rstd::move(checked).unwrap_err_unchecked())));
                    else
                        result = Some(Decoded(Ok(V::make())));
                } else {
                    auto value = input.template value<typename V::payload_type>();
                    if (value.is_err())
                        result = Some(Decoded(Err(rstd::move(value).unwrap_err_unchecked())));
                    else
                        result = Some(Decoded(Ok(V::make(rstd::move(value).unwrap_unchecked()))));
                }
            }
        });
        if (result.is_some()) return rstd::move(result).unwrap_unchecked();
        return Err(unknown());
    }
    template<typename D, EnumAccess Input>
    static auto deserialize_variant(D& source, Input& input, typename D::error_type unknown)
        -> Result<T, typename D::error_type> {
        return deserialize_variant(source, input, [&] {
            return rstd::move(unknown);
        });
    }
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template variants<VariantDefinition>();
    }
};

template<typename T, Text Name, Direction Mode>
struct UntaggedDefinition : VariantDefinition<T, Name, Mode> {
    template<typename S>
    static auto serialize(S& sink, const T& value) -> typename S::result_type {
        auto result = typename S::result_type(Err(sink.invalid_value("Unknown variant"_str)));
        UntaggedDefinition::variants([&]<typename V>() {
            if (! V::matches(value)) return;
            if constexpr (__is_void(typename V::payload_type))
                result = sink.serialize_unit();
            else
                result = serde::serialize(sink, V::get(value));
        });
        return result;
    }

    template<typename D>
    static auto deserialize(D& source) -> Result<T, typename D::error_type> {
        if constexpr (! CapturingDeserializer<D>) {
            return Err(
                source.unsupported("untagged decoding requires semantic capture and replay"_str));
        } else {
            auto captured = source.capture_value();
            if (captured.is_err()) return Err(rstd::move(captured).unwrap_err_unchecked());
            Option<T>                      decoded;
            Option<typename D::error_type> failure;
            UntaggedDefinition::variants([&]<typename V>() {
                if (decoded.is_some()) return;
                auto replay   = captured->replay();
                auto complete = [&]() -> bool {
                    if constexpr (requires { captured->finish(replay); }) {
                        auto ended = captured->finish(replay);
                        if (ended.is_err()) {
                            failure = Some(rstd::move(ended).unwrap_err_unchecked());
                            return false;
                        }
                    }
                    return true;
                };
                if constexpr (__is_void(typename V::payload_type)) {
                    auto result = replay.deserialize_unit();
                    if (result.is_ok()) {
                        if (complete()) decoded = Some(V::make());
                    } else
                        failure = Some(rstd::move(result).unwrap_err_unchecked());
                } else {
                    auto result = serde::deserialize<typename V::payload_type>(replay);
                    if (result.is_ok()) {
                        if (complete())
                            decoded = Some(V::make(rstd::move(result).unwrap_unchecked()));
                    } else
                        failure = Some(rstd::move(result).unwrap_err_unchecked());
                }
            });
            if (decoded.is_some()) return Ok(rstd::move(decoded).unwrap_unchecked());
            if (failure.is_some()) {
                if constexpr (requires {
                                  source.invalid_value_with_source(
                                      "input did not match any untagged variant"_str,
                                      rstd::move(failure).unwrap_unchecked());
                              })
                    return Err(source.invalid_value_with_source(
                        "input did not match any untagged variant"_str,
                        rstd::move(failure).unwrap_unchecked()));
                else
                    return Err(rstd::move(failure).unwrap_unchecked());
            }
            return Err(source.invalid_value("input did not match any untagged variant"_str));
        }
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

template<auto Pointer, Text Name, FieldPolicy Policy = FieldPolicy::Required, Text... Aliases>
struct PayloadMemberOptions : Member<Pointer, Name, Policy, Aliases...> {};

template<typename Field, typename... Options>
struct DescribedMember : Member<Field::pointer, Text(Field::name_literal.data)> {};
template<typename Field, typename First, typename... Rest>
struct DescribedMember<Field, First, Rest...> : mtp::cond<[] {
    if constexpr (mtp::same_as<decltype(Field::pointer), decltype(First::pointer)>)
        return Field::pointer == First::pointer;
    else
        return false;
}(),
                                                          First,
                                                          DescribedMember<Field, Rest...>> {};

template<typename T, Text Name, Direction Mode, UnknownFieldPolicy Unknown, typename... Options>
struct DescribedPayloadDefinition {
    using model_type                     = T;
    static constexpr auto name           = Name;
    static constexpr bool serializable   = Mode != Direction::Deserialize;
    static constexpr bool deserializable = Mode != Direction::Serialize;

    template<typename Left, typename Right>
    static constexpr auto same_pointer() -> bool {
        if constexpr (mtp::same_as<decltype(Left::pointer), decltype(Right::pointer)>)
            return Left::pointer == Right::pointer;
        else
            return false;
    }
    static consteval auto valid_options() -> bool {
        using Definition = PayloadDefinition<T>;
        bool       valid = true;
        const auto known = [&]<typename Setting>() {
            size_t found = 0;
            Definition::fields([&]<typename Field>() {
                if (same_pointer<Setting, Field>()) ++found;
            });
            return found == 1;
        };
        valid = (known.template operator()<Options>() && ...);
        Definition::fields([&]<typename Field>() {
            size_t selected = (size_t(0) + ... + size_t(same_pointer<Field, Options>()));
            valid           = valid && selected <= 1;
        });
        return valid;
    }
    static consteval auto valid_record_names() -> bool {
        using Definition = PayloadDefinition<T>;
        bool   valid     = true;
        size_t outer     = 0;
        Definition::fields([&]<typename Left>() {
            using A      = DescribedMember<Left, Options...>;
            size_t inner = 0;
            if constexpr (A::policy != FieldPolicy::Skip) {
                A::aliases([&](auto alias) {
                    if (same_text(A::name, alias)) valid = false;
                    size_t matches = 0;
                    A::aliases([&](auto other) {
                        if (same_text(alias, other)) ++matches;
                    });
                    if (matches != 1) valid = false;
                });
                Definition::fields([&]<typename Right>() {
                    using B = DescribedMember<Right, Options...>;
                    if constexpr (B::policy != FieldPolicy::Skip) {
                        if (inner > outer) {
                            if (same_text(A::name, B::name)) valid = false;
                            A::aliases([&](auto alias) {
                                if (same_text(alias, B::name)) valid = false;
                                B::aliases([&](auto other) {
                                    if (same_text(alias, other)) valid = false;
                                });
                            });
                            B::aliases([&](auto alias) {
                                if (same_text(A::name, alias)) valid = false;
                            });
                        }
                    }
                    ++inner;
                });
            }
            ++outer;
        });
        return valid;
    }

    template<size_t... I>
    static auto record_type(mtp::index_sequence<I...>) -> RecordDefinition<
        T,
        Name,
        Mode,
        Unknown,
        DescribedMember<typename PayloadDefinition<T>::template field_at<I>, Options...>...>;
    template<typename D, typename Sequence, size_t I, typename... Values>
    static auto read_sequence(D& source, Sequence& input, Values&&... values)
        -> Result<T, typename D::error_type> {
        using Definition = PayloadDefinition<T>;
        if constexpr (I == Definition::field_count) {
            auto ended = input.end();
            if (ended.is_err()) return Err(rstd::move(ended).unwrap_err_unchecked());
            return Ok(Definition::make(rstd::forward<Values>(values)...));
        } else {
            using Field = typename Definition::template field_at<I>;
            auto next   = input.template next<typename Field::type>();
            if (next.is_err()) return Err(rstd::move(next).unwrap_err_unchecked());
            auto optional = rstd::move(next).unwrap_unchecked();
            if (optional.is_none())
                return Err(source.invalid_value("tuple element is missing"_str));
            return read_sequence<D, Sequence, I + 1>(source,
                                                     input,
                                                     rstd::forward<Values>(values)...,
                                                     rstd::move(optional).unwrap_unchecked());
        }
    }
    template<typename S>
    static auto serialize(S& sink, const T& value) -> typename S::result_type {
        static_assert(DescribedPayload<T>, "serde described_payload requires typed metadata");
        static_assert(valid_options(), "serde payload field options must be known and unique");
        using Definition = PayloadDefinition<T>;
        if constexpr (Definition::kind == VariantKind::Record) {
            static_assert(valid_record_names(), "serde payload field names overlap");
            using Record =
                decltype(record_type(mtp::make_index_sequence<Definition::field_count> {}));
            return Record::serialize(sink, value);
        } else {
            static_assert(sizeof...(Options) == 0,
                          "serde tuple and unit payload fields do not accept record options");
            if constexpr (Definition::kind == VariantKind::Unit)
                return sink.serialize_unit();
            else {
                static_assert(Definition::kind == VariantKind::Tuple,
                              "serde described_payload requires unit, tuple or record metadata");
                if constexpr (Definition::field_count == 1) {
                    using Field = typename Definition::template field_at<0>;
                    return sink.serialize_newtype(Name.view(), Field::get(value));
                } else {
                    auto started = sink.begin_sequence(usize(Definition::field_count));
                    if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
                    auto output = rstd::move(started).unwrap_unchecked();
                    auto result = Result<empty, typename S::error_type>(Ok(empty {}));
                    Definition::fields([&]<typename Field>() {
                        if (result.is_ok()) result = output.element(Field::get(value));
                    });
                    if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
                    return output.end();
                }
            }
        }
    }
    template<typename D>
    static auto deserialize(D& source) -> Result<T, typename D::error_type> {
        static_assert(DescribedPayload<T>, "serde described_payload requires typed metadata");
        static_assert(valid_options(), "serde payload field options must be known and unique");
        using Definition = PayloadDefinition<T>;
        if constexpr (Definition::kind == VariantKind::Record) {
            static_assert(valid_record_names(), "serde payload field names overlap");
            using Record =
                decltype(record_type(mtp::make_index_sequence<Definition::field_count> {}));
            return Record::deserialize_declared(source);
        } else {
            static_assert(sizeof...(Options) == 0,
                          "serde tuple and unit payload fields do not accept record options");
            if constexpr (Definition::kind == VariantKind::Unit) {
                auto checked = source.deserialize_unit();
                if (checked.is_err()) return Err(rstd::move(checked).unwrap_err_unchecked());
                return Ok(Definition::make());
            } else {
                static_assert(Definition::kind == VariantKind::Tuple,
                              "serde described_payload requires unit, tuple or record metadata");
                if constexpr (Definition::field_count == 1) {
                    using Field = typename Definition::template field_at<0>;
                    auto decoded =
                        source.template deserialize_newtype<typename Field::type>(Name.view());
                    if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err_unchecked());
                    return Ok(Definition::make(rstd::move(decoded).unwrap_unchecked()));
                } else {
                    auto started = source.begin_sequence();
                    if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
                    auto input = rstd::move(started).unwrap_unchecked();
                    return read_sequence<D, decltype(input), 0>(source, input);
                }
            }
        }
    }
    template<typename D>
    static auto deserialize_declared(D& source) -> Result<T, typename D::error_type> {
        return deserialize(source);
    }
    template<bool Inherit, typename D, typename Construct>
    static auto deserialize_constructed(D& source, Construct construct)
        -> Result<T, typename D::error_type> {
        using Definition = PayloadDefinition<T>;
        if constexpr (Definition::kind == VariantKind::Record) {
            static_assert(valid_options(), "serde payload field options must be known and unique");
            static_assert(valid_record_names(), "serde payload field names overlap");
            using Record =
                decltype(record_type(mtp::make_index_sequence<Definition::field_count> {}));
            return Record::template deserialize_constructed<false>(source, construct);
        } else {
            return deserialize(source);
        }
    }
    template<typename Sink, size_t... I>
    static constexpr void describe_tuple(Sink& sink, mtp::index_sequence<I...>) {
        sink.template tuple<typename PayloadDefinition<T>::template field_at<I>::type...>();
    }
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        using Definition = PayloadDefinition<T>;
        if constexpr (Definition::kind == VariantKind::Record) {
            using Record =
                decltype(record_type(mtp::make_index_sequence<Definition::field_count> {}));
            Record::describe(sink);
        } else if constexpr (Definition::kind == VariantKind::Tuple && Definition::field_count == 1)
            serde::describe<typename Definition::template field_at<0>::type>(sink);
        else if constexpr (Definition::kind == VariantKind::Unit)
            sink.unit();
        else {
            static_assert(Definition::kind == VariantKind::Tuple,
                          "serde described_payload requires unit, tuple or record metadata");
            describe_tuple(sink, mtp::make_index_sequence<Definition::field_count> {});
        }
    }
};

template<typename S>
class InternalTagMap {
    typename S::map_type map_;
    S*                   sink_;
    ref<str>             tag_;

public:
    using error_type = typename S::error_type;

    InternalTagMap(typename S::map_type map, S& sink, ref<str> tag)
        : map_(rstd::move(map)), sink_(&sink), tag_(tag) {}

    template<typename T>
    auto key(const T& value) -> Result<empty, error_type> {
        if constexpr (mtp::same_as<T, ref<str>>) {
            if (value == tag_)
                return Err(sink_->invalid_value("internal tag conflicts with a payload field"_str));
        } else if constexpr (mtp::same_as<T, ::alloc::string::String>) {
            if (value.as_str() == tag_)
                return Err(sink_->invalid_value("internal tag conflicts with a payload field"_str));
        }
        return map_.key(value);
    }
    template<typename T>
    auto value(const T& value) -> Result<empty, error_type> {
        return map_.value(value);
    }
    auto end() -> typename S::result_type { return map_.end(); }
};

template<typename S>
class InternalTagSerializer {
    S*       sink_;
    ref<str> tag_;
    ref<str> variant_;

    auto unsupported_payload() -> typename S::result_type {
        return Err(sink_->unsupported("internally tagged payload must serialize as a map"_str));
    }

public:
    using value_type    = typename S::value_type;
    using error_type    = typename S::error_type;
    using result_type   = typename S::result_type;
    using sequence_type = typename S::sequence_type;
    using map_type      = InternalTagMap<S>;

    InternalTagSerializer(S& sink, ref<str> tag, ref<str> variant)
        : sink_(&sink), tag_(tag), variant_(variant) {}

    auto serialize_none() -> result_type { return unsupported_payload(); }
    auto serialize_unit() -> result_type { return unsupported_payload(); }
    auto serialize_bool(bool) -> result_type { return unsupported_payload(); }
    auto serialize_i64(i64) -> result_type { return unsupported_payload(); }
    auto serialize_u64(u64) -> result_type { return unsupported_payload(); }
    auto serialize_f64(f64) -> result_type { return unsupported_payload(); }
    auto serialize_string(ref<str>) -> result_type { return unsupported_payload(); }
    auto serialize_bytes(slice<u8>) -> result_type { return unsupported_payload(); }
    auto serialize_unit_variant(ref<str>) -> result_type { return unsupported_payload(); }
    template<typename T>
    auto serialize_some(const T&) -> result_type {
        return unsupported_payload();
    }
    template<typename T>
    auto serialize_newtype(ref<str>, const T& value) -> result_type {
        return serde::serialize(*this, value);
    }
    template<typename T>
    auto serialize_newtype_variant(ref<str>, const T&) -> result_type {
        return unsupported_payload();
    }
    template<typename T>
    auto serialize_extension(ref<str>, const T&) -> result_type {
        return unsupported_payload();
    }
    auto begin_sequence(usize) -> Result<sequence_type, error_type> {
        return Err(sink_->unsupported("internally tagged payload must serialize as a map"_str));
    }
    auto begin_map(usize len) -> Result<map_type, error_type> {
        auto started = sink_->begin_map(len + usize(1));
        if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
        auto map = rstd::move(started).unwrap_unchecked();
        auto tag = field(map, tag_, variant_);
        if (tag.is_err()) return Err(rstd::move(tag).unwrap_err_unchecked());
        return Ok(map_type(rstd::move(map), *sink_, tag_));
    }
    auto invalid_value(ref<str> message) const -> error_type {
        return sink_->invalid_value(message);
    }
    auto unsupported(ref<str> message) const -> error_type { return sink_->unsupported(message); }
};

template<typename T,
         Text               Name,
         Direction          Mode,
         Text               Tag,
         UnknownFieldPolicy Unknown = UnknownFieldPolicy::Reject,
         typename... Names>
struct InternalVariantDefinition : VariantDefinition<T, Name, Mode, Names...> {
    static constexpr auto tag            = Tag;
    static constexpr auto unknown_fields = Unknown;

    template<typename S>
    static auto serialize(S& sink, const T& value) -> typename S::result_type {
        auto result = typename S::result_type(Err(sink.invalid_value("unknown variant"_str)));
        InternalVariantDefinition::variants([&]<typename V>() {
            if (! V::matches(value)) return;
            if constexpr (__is_void(typename V::payload_type)) {
                auto started = sink.begin_map(usize(1));
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
                result = map.end();
            } else {
                constexpr auto variant = Text(V::name);
                auto           tagged  = InternalTagSerializer(sink, Tag.view(), variant.view());
                result                 = serde::serialize(tagged, V::get(value));
            }
        });
        return result;
    }
    template<InternalVariantDeserializer D>
    static auto deserialize(D& source) -> Result<T, typename D::error_type> {
        auto started = source.begin_internal_enum(Tag.view(), Unknown);
        if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
        auto input = rstd::move(started).unwrap_unchecked();
        // Tags are untrusted and may contain credentials.
        return InternalVariantDefinition::deserialize_variant(source, input, [&] {
            return source.unknown_variant("unrecognized"_str);
        });
    }
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template variants<InternalVariantDefinition>();
    }
};

template<typename T,
         Text               Name,
         Direction          Mode,
         Text               Tag,
         Text               Content,
         UnknownFieldPolicy Unknown = UnknownFieldPolicy::Reject,
         typename... Names>
struct AdjacentVariantDefinition : VariantDefinition<T, Name, Mode, Names...> {
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
    template<AdjacentVariantDeserializer D>
    static auto deserialize(D& source) -> Result<T, typename D::error_type> {
        auto started = source.begin_adjacent_enum(Tag.view(), Content.view(), Unknown);
        if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
        auto input = rstd::move(started).unwrap_unchecked();
        return AdjacentVariantDefinition::deserialize_variant(source, input, [&] {
            return source.unknown_variant("unrecognized"_str);
        });
    }
    template<typename Sink>
    static constexpr void describe(Sink& sink) {
        sink.template variants<AdjacentVariantDefinition>();
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

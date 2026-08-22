export module rstd.serde:traits;
export import :error;

using namespace rstd::prelude;

export namespace rstd::serde
{

struct Serialize final {};
struct Deserialize final {};

template<typename T, typename ResultType>
concept SequenceSerializer = requires(T& sequence, bool value) {
    typename T::error_type;
    { sequence.element(value) } -> mtp::same_as<Result<empty, typename T::error_type>>;
    { sequence.end() } -> mtp::same_as<ResultType>;
};

template<typename T, typename ResultType>
concept MapSerializer = requires(T& map, bool value) {
    typename T::error_type;
    { map.key(value) } -> mtp::same_as<Result<empty, typename T::error_type>>;
    { map.value(value) } -> mtp::same_as<Result<empty, typename T::error_type>>;
    { map.end() } -> mtp::same_as<ResultType>;
};

template<typename T>
concept Serializer = requires(T&        serializer,
                              bool      boolean,
                              i64       signed_integer,
                              u64       unsigned_integer,
                              f64       floating,
                              ref<str>  text,
                              slice<u8> bytes,
                              usize     len) {
    typename T::value_type;
    typename T::error_type;
    typename T::result_type;
    typename T::sequence_type;
    typename T::map_type;
    requires mtp::same_as<typename T::result_type,
                          Result<typename T::value_type, typename T::error_type>>;
    requires mtp::same_as<typename T::sequence_type::error_type, typename T::error_type>;
    requires mtp::same_as<typename T::map_type::error_type, typename T::error_type>;
    requires SequenceSerializer<typename T::sequence_type, typename T::result_type>;
    requires MapSerializer<typename T::map_type, typename T::result_type>;
    { serializer.serialize_none() } -> mtp::same_as<typename T::result_type>;
    { serializer.serialize_unit() } -> mtp::same_as<typename T::result_type>;
    { serializer.serialize_bool(boolean) } -> mtp::same_as<typename T::result_type>;
    { serializer.serialize_i64(signed_integer) } -> mtp::same_as<typename T::result_type>;
    { serializer.serialize_u64(unsigned_integer) } -> mtp::same_as<typename T::result_type>;
    { serializer.serialize_f64(floating) } -> mtp::same_as<typename T::result_type>;
    { serializer.serialize_string(text) } -> mtp::same_as<typename T::result_type>;
    { serializer.serialize_bytes(bytes) } -> mtp::same_as<typename T::result_type>;
    { serializer.serialize_some(boolean) } -> mtp::same_as<typename T::result_type>;
    { serializer.serialize_newtype(text, boolean) } -> mtp::same_as<typename T::result_type>;
    { serializer.serialize_unit_variant(text) } -> mtp::same_as<typename T::result_type>;
    {
        serializer.serialize_newtype_variant(text, boolean)
    } -> mtp::same_as<typename T::result_type>;
    {
        serializer.begin_sequence(len)
    } -> mtp::same_as<Result<typename T::sequence_type, typename T::error_type>>;
    {
        serializer.begin_map(len)
    } -> mtp::same_as<Result<typename T::map_type, typename T::error_type>>;
    { serializer.invalid_value(text) } -> mtp::same_as<typename T::error_type>;
    { serializer.unsupported(text) } -> mtp::same_as<typename T::error_type>;
};

template<typename T>
concept SequenceAccess = requires(T& sequence) {
    typename T::error_type;
    {
        sequence.template next<bool>()
    } -> mtp::same_as<Result<Option<bool>, typename T::error_type>>;
    { sequence.end() } -> mtp::same_as<Result<empty, typename T::error_type>>;
};

template<typename T>
concept MapAccess = requires(T& map) {
    typename T::error_type;
    { map.template next_key<bool>() } -> mtp::same_as<Result<Option<bool>, typename T::error_type>>;
    { map.template next_value<bool>() } -> mtp::same_as<Result<bool, typename T::error_type>>;
    { map.ignore_value() } -> mtp::same_as<Result<empty, typename T::error_type>>;
    { map.end() } -> mtp::same_as<Result<empty, typename T::error_type>>;
};

template<typename T>
concept EnumAccess = requires(T& value) {
    typename T::error_type;
    { value.variant() } -> mtp::same_as<ref<str>>;
    { value.unit() } -> mtp::same_as<Result<empty, typename T::error_type>>;
    { value.template value<bool>() } -> mtp::same_as<Result<bool, typename T::error_type>>;
};

template<typename T>
concept Deserializer = requires(T& deserializer, ref<str> name) {
    typename T::error_type;
    typename T::sequence_type;
    typename T::map_type;
    typename T::enum_type;
    requires mtp::same_as<typename T::sequence_type::error_type, typename T::error_type>;
    requires mtp::same_as<typename T::map_type::error_type, typename T::error_type>;
    requires mtp::same_as<typename T::enum_type::error_type, typename T::error_type>;
    requires SequenceAccess<typename T::sequence_type>;
    requires MapAccess<typename T::map_type>;
    requires EnumAccess<typename T::enum_type>;
    { deserializer.deserialize_unit() } -> mtp::same_as<Result<empty, typename T::error_type>>;
    { deserializer.deserialize_bool() } -> mtp::same_as<Result<bool, typename T::error_type>>;
    { deserializer.deserialize_i64() } -> mtp::same_as<Result<i64, typename T::error_type>>;
    { deserializer.deserialize_u64() } -> mtp::same_as<Result<u64, typename T::error_type>>;
    { deserializer.deserialize_f64() } -> mtp::same_as<Result<f64, typename T::error_type>>;
    {
        deserializer.deserialize_string()
    } -> mtp::same_as<Result<::alloc::string::String, typename T::error_type>>;
    {
        deserializer.deserialize_bytes()
    } -> mtp::same_as<Result<::alloc::vec::Vec<u8>, typename T::error_type>>;
    {
        deserializer.template deserialize_option<bool>()
    } -> mtp::same_as<Result<Option<bool>, typename T::error_type>>;
    {
        deserializer.template deserialize_newtype<bool>(name)
    } -> mtp::same_as<Result<bool, typename T::error_type>>;
    {
        deserializer.begin_sequence()
    } -> mtp::same_as<Result<typename T::sequence_type, typename T::error_type>>;
    {
        deserializer.begin_map()
    } -> mtp::same_as<Result<typename T::map_type, typename T::error_type>>;
    {
        deserializer.begin_enum()
    } -> mtp::same_as<Result<typename T::enum_type, typename T::error_type>>;
    { deserializer.invalid_value(name) } -> mtp::same_as<typename T::error_type>;
    { deserializer.missing_field(name) } -> mtp::same_as<typename T::error_type>;
    { deserializer.unknown_field(name) } -> mtp::same_as<typename T::error_type>;
    { deserializer.unknown_variant(name) } -> mtp::same_as<typename T::error_type>;
    { deserializer.duplicate_field(name) } -> mtp::same_as<typename T::error_type>;
    { deserializer.invariant(name) } -> mtp::same_as<typename T::error_type>;
    { deserializer.unsupported(name) } -> mtp::same_as<typename T::error_type>;
    { deserializer.ignore_value() } -> mtp::same_as<Result<empty, typename T::error_type>>;
};

template<typename T>
concept Serializable = mtp::complete<Impl<Serialize, mtp::rm_cvf<T>>>;

template<typename T>
concept Deserializable = mtp::complete<Impl<Deserialize, mtp::rm_cvf<T>>>;

template<typename T>
constexpr bool missing_serialize_impl = false;

template<typename T>
constexpr bool missing_deserialize_impl = false;

template<typename Serializer, typename T>
decltype(auto) serialize(Serializer& serializer, const T& value) {
    using Type = mtp::rm_cvf<T>;
    if constexpr (Serializable<Type>) {
        return Impl<Serialize, Type>::serialize(serializer, value);
    } else {
        static_assert(missing_serialize_impl<Type>,
                      "rstd::serde::serialize requires an explicit Impl<Serialize, T>");
    }
}

template<typename T, typename Deserializer>
decltype(auto) deserialize(Deserializer& deserializer) {
    using Type = mtp::rm_cvf<T>;
    if constexpr (Deserializable<Type>) {
        return Impl<Deserialize, Type>::deserialize(deserializer);
    } else {
        static_assert(missing_deserialize_impl<Type>,
                      "rstd::serde::deserialize requires an explicit Impl<Deserialize, T>");
    }
}

} // namespace rstd::serde

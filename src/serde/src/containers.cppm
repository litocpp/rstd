export module rstd.serde:containers;
export import :primitives;

using namespace rstd::prelude;
using ::alloc::collections::BTreeMap;
using ::alloc::vec::Vec;

export namespace rstd::serde
{
template<typename T>
struct TaggedOption {
    Option<T> value;
};
} // namespace rstd::serde

namespace rstd
{

template<typename T, typename E>
    requires serde::Serializable<T> && serde::Serializable<E>
struct Impl<serde::Serialize, Result<T, E>> {
    template<typename S>
    static auto serialize(S& sink, const Result<T, E>& value) -> typename S::result_type {
        if (value.is_ok()) return sink.serialize_newtype_variant("Ok"_str, *value);
        return sink.serialize_newtype_variant("Err"_str, value.as_ref().unwrap_err_unchecked());
    }
};
template<typename T, typename E>
    requires serde::Deserializable<T> && serde::Deserializable<E>
struct Impl<serde::Deserialize, Result<T, E>> {
    template<typename D>
    static auto deserialize(D& source) -> Result<Result<T, E>, typename D::error_type> {
        auto started = source.begin_enum();
        if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
        auto input = rstd::move(started).unwrap_unchecked();
        if (input.variant() == "Ok"_str) {
            auto value = input.template value<T>();
            if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
            return Ok(Result<T, E>(Ok(rstd::move(value).unwrap_unchecked())));
        }
        if (input.variant() == "Err"_str) {
            auto value = input.template value<E>();
            if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
            return Ok(Result<T, E>(Err(rstd::move(value).unwrap_unchecked())));
        }
        return Err(source.unknown_variant(input.variant()));
    }
};
template<typename T>
    requires serde::Serializable<T>
struct Impl<serde::Serialize, serde::TaggedOption<T>> {
    template<typename S>
    static auto serialize(S& sink, const serde::TaggedOption<T>& value) -> typename S::result_type {
        if (value.value.is_none()) return sink.serialize_unit_variant("None"_str);
        return sink.serialize_newtype_variant("Some"_str, *value.value);
    }
};
template<typename T>
    requires serde::Deserializable<T>
struct Impl<serde::Deserialize, serde::TaggedOption<T>> {
    template<typename D>
    static auto deserialize(D& source) -> Result<serde::TaggedOption<T>, typename D::error_type> {
        auto started = source.begin_enum();
        if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
        auto input = rstd::move(started).unwrap_unchecked();
        if (input.variant() == "None"_str) {
            auto checked = input.unit();
            if (checked.is_err()) return Err(rstd::move(checked).unwrap_err_unchecked());
            return Ok(serde::TaggedOption<T> { None<T>() });
        }
        if (input.variant() == "Some"_str) {
            auto value = input.template value<T>();
            if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
            return Ok(serde::TaggedOption<T> { Some(rstd::move(value).unwrap_unchecked()) });
        }
        return Err(source.unknown_variant(input.variant()));
    }
};

template<typename T>
    requires serde::Serializable<T>
struct Impl<serde::Serialize, Option<T>> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const Option<T>& value) ->
        typename Serializer::result_type {
        if (value.is_none()) return serializer.serialize_none();
        return serializer.serialize_some(*value);
    }
};

template<typename T>
    requires serde::Deserializable<T>
struct Impl<serde::Deserialize, Option<T>> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<Option<T>, typename Deserializer::error_type> {
        return deserializer.template deserialize_option<T>();
    }
};

template<typename T>
    requires serde::Serializable<T>
struct Impl<serde::Serialize, Vec<T>> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const Vec<T>& value) ->
        typename Serializer::result_type {
        auto sequence = serializer.begin_sequence(value.len());
        if (sequence.is_err()) return Err(rstd::move(sequence).unwrap_err_unchecked());
        auto output = rstd::move(sequence).unwrap_unchecked();
        for (const auto& element : value) {
            auto result = output.element(element);
            if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
        }
        return output.end();
    }
};

template<typename T>
    requires serde::Deserializable<T>
struct Impl<serde::Deserialize, Vec<T>> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<Vec<T>, typename Deserializer::error_type> {
        auto sequence = deserializer.begin_sequence();
        if (sequence.is_err()) return Err(rstd::move(sequence).unwrap_err_unchecked());
        auto input  = rstd::move(sequence).unwrap_unchecked();
        auto values = Vec<T>::make();
        for (;;) {
            auto next = input.template next<T>();
            if (next.is_err()) return Err(rstd::move(next).unwrap_err_unchecked());
            auto optional = rstd::move(next).unwrap_unchecked();
            if (optional.is_none()) break;
            values.push(rstd::move(optional).unwrap_unchecked());
        }
        auto end = input.end();
        if (end.is_err()) return Err(rstd::move(end).unwrap_err_unchecked());
        return Ok(rstd::move(values));
    }
};

template<typename K, typename V>
    requires serde::Serializable<K> && serde::Serializable<V>
struct Impl<serde::Serialize, BTreeMap<K, V>> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const BTreeMap<K, V>& value) ->
        typename Serializer::result_type {
        auto map = serializer.begin_map(value.len());
        if (map.is_err()) return Err(rstd::move(map).unwrap_err_unchecked());
        auto output = rstd::move(map).unwrap_unchecked();
        for (auto entry : value.iter()) {
            auto key = output.key(*entry.template get<0>());
            if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
            auto item = output.value(*entry.template get<1>());
            if (item.is_err()) return Err(rstd::move(item).unwrap_err_unchecked());
        }
        return output.end();
    }
};

template<typename K, typename V>
    requires serde::Deserializable<K> && serde::Deserializable<V>
struct Impl<serde::Deserialize, BTreeMap<K, V>> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<BTreeMap<K, V>, typename Deserializer::error_type> {
        auto map = [&] {
            if constexpr (requires { deserializer.begin_dictionary(); })
                return deserializer.begin_dictionary();
            else
                return deserializer.begin_map();
        }();
        if (map.is_err()) return Err(rstd::move(map).unwrap_err_unchecked());
        auto input  = rstd::move(map).unwrap_unchecked();
        auto values = BTreeMap<K, V>::make();
        for (;;) {
            auto key = input.template next_key<K>();
            if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
            auto optional = rstd::move(key).unwrap_unchecked();
            if (optional.is_none()) break;
            auto value = input.template next_value<V>();
            if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
            auto replaced = values.insert(rstd::move(optional).unwrap_unchecked(),
                                          rstd::move(value).unwrap_unchecked());
            if (replaced.is_some()) {
                return Err(deserializer.invalid_value("map contains a duplicate key"_str));
            }
        }
        auto end = input.end();
        if (end.is_err()) return Err(rstd::move(end).unwrap_err_unchecked());
        return Ok(rstd::move(values));
    }
};

} // namespace rstd

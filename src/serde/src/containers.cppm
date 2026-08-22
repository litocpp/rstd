export module rstd.serde:containers;
export import :primitives;

using namespace rstd::prelude;
using ::alloc::collections::BTreeMap;
using ::alloc::vec::Vec;

namespace rstd
{

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
        auto map = deserializer.begin_map();
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

export module rstd.serde:tuple;
export import :containers;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace rstd::serde
{

template<size_t I, typename Sequence, typename... Ts>
auto serialize_tuple_elements(Sequence& sequence, const tuple<Ts...>& value)
    -> Result<empty, typename Sequence::error_type> {
    if constexpr (I == sizeof...(Ts)) {
        return sequence.end();
    } else {
        auto result = sequence.element(value.template get<I>());
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
        return serialize_tuple_elements<I + 1>(sequence, value);
    }
}

template<size_t I, typename Deserializer, typename Sequence, typename... Ts, typename... Values>
auto deserialize_tuple_elements(Deserializer& deserializer, Sequence& sequence, Values&&... values)
    -> Result<tuple<Ts...>, typename Deserializer::error_type> {
    if constexpr (I == sizeof...(Ts)) {
        auto end = sequence.end();
        if (end.is_err()) return Err(rstd::move(end).unwrap_err_unchecked());
        return Ok(tuple<Ts...>(rstd::forward<Values>(values)...));
    } else {
        using Element = mtp::tuple_element<I, tuple<Ts...>>;
        auto next     = sequence.template next<Element>();
        if (next.is_err()) return Err(rstd::move(next).unwrap_err_unchecked());
        auto optional = rstd::move(next).unwrap_unchecked();
        if (optional.is_none()) {
            return Err(deserializer.invalid_value("tuple element is missing"_str));
        }
        return deserialize_tuple_elements<I + 1, Deserializer, Sequence, Ts...>(
            deserializer,
            sequence,
            rstd::forward<Values>(values)...,
            rstd::move(optional).unwrap_unchecked());
    }
}

} // namespace rstd::serde

namespace rstd
{

template<typename... Ts>
    requires(serde::Serializable<Ts> && ...)
struct Impl<serde::Serialize, tuple<Ts...>> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const tuple<Ts...>& value) ->
        typename Serializer::result_type {
        auto sequence = serializer.begin_sequence(usize(sizeof...(Ts)));
        if (sequence.is_err()) return Err(rstd::move(sequence).unwrap_err_unchecked());
        auto output = rstd::move(sequence).unwrap_unchecked();
        return serde::serialize_tuple_elements<0>(output, value);
    }
};

template<typename... Ts>
    requires(serde::Deserializable<Ts> && ...)
struct Impl<serde::Deserialize, tuple<Ts...>> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<tuple<Ts...>, typename Deserializer::error_type> {
        auto sequence = deserializer.begin_sequence();
        if (sequence.is_err()) return Err(rstd::move(sequence).unwrap_err_unchecked());
        auto input = rstd::move(sequence).unwrap_unchecked();
        return serde::deserialize_tuple_elements<0, Deserializer, decltype(input), Ts...>(
            deserializer, input);
    }
};

} // namespace rstd

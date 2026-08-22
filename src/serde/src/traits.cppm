export module rstd.serde:traits;
export import :error;

using namespace rstd::prelude;

export namespace rstd::serde
{

struct Serialize final {};
struct Deserialize final {};

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

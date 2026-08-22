export module rstd.serde:primitives;
export import :traits;
export import rstd.alloc;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;

export namespace rstd::serde
{

struct Ignored {};

} // namespace rstd::serde

namespace rstd
{

template<>
struct Impl<serde::Serialize, empty> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, empty) {
        return serializer.serialize_unit();
    }
};

template<>
struct Impl<serde::Deserialize, empty> {
    template<typename Deserializer>
    static decltype(auto) deserialize(Deserializer& deserializer) {
        return deserializer.deserialize_unit();
    }
};

template<>
struct Impl<serde::Deserialize, serde::Ignored> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<serde::Ignored, typename Deserializer::error_type> {
        auto result = deserializer.ignore_value();
        if (result.is_err()) return Err(rstd::move(result).unwrap_err_unchecked());
        return Ok(serde::Ignored {});
    }
};

template<>
struct Impl<serde::Serialize, bool> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, bool value) {
        return serializer.serialize_bool(value);
    }
};

template<>
struct Impl<serde::Deserialize, bool> {
    template<typename Deserializer>
    static decltype(auto) deserialize(Deserializer& deserializer) {
        return deserializer.deserialize_bool();
    }
};

template<num::SignedInteger T>
    requires(sizeof(T) <= sizeof(i64))
struct Impl<serde::Serialize, T> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, T value) {
        return serializer.serialize_i64(rstd::as_cast<i64>(value));
    }
};

template<num::UnsignedInteger T>
    requires(sizeof(T) <= sizeof(u64))
struct Impl<serde::Serialize, T> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, T value) {
        return serializer.serialize_u64(rstd::as_cast<u64>(value));
    }
};

template<num::SignedInteger T>
    requires(sizeof(T) <= sizeof(i64))
struct Impl<serde::Deserialize, T> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<T, typename Deserializer::error_type> {
        auto value = deserializer.deserialize_i64();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        auto converted = rstd::try_from<T>(rstd::move(value).unwrap_unchecked());
        if (converted.is_err()) {
            return Err(deserializer.invalid_value("integer out of range"_str));
        }
        return Ok(rstd::move(converted).unwrap_unchecked());
    }
};

template<num::UnsignedInteger T>
    requires(sizeof(T) <= sizeof(u64))
struct Impl<serde::Deserialize, T> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<T, typename Deserializer::error_type> {
        auto value = deserializer.deserialize_u64();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        auto converted = rstd::try_from<T>(rstd::move(value).unwrap_unchecked());
        if (converted.is_err()) {
            return Err(deserializer.invalid_value("integer out of range"_str));
        }
        return Ok(rstd::move(converted).unwrap_unchecked());
    }
};

template<num::Float T>
struct Impl<serde::Serialize, T> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, T value) {
        return serializer.serialize_f64(rstd::as_cast<f64>(value));
    }
};

template<num::Float T>
struct Impl<serde::Deserialize, T> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> Result<T, typename Deserializer::error_type> {
        auto value = deserializer.deserialize_f64();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(rstd::as_cast<T>(rstd::move(value).unwrap_unchecked()));
    }
};

template<>
struct Impl<serde::Serialize, String> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, const String& value) {
        return serializer.serialize_string(value.as_str());
    }
};

template<>
struct Impl<serde::Deserialize, String> {
    template<typename Deserializer>
    static decltype(auto) deserialize(Deserializer& deserializer) {
        return deserializer.deserialize_string();
    }
};

template<>
struct Impl<serde::Serialize, ref<str>> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, ref<str> value) {
        return serializer.serialize_string(value);
    }
};

} // namespace rstd

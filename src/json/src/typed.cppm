export module rstd.json:typed;
export import :parser;
export import :serialize;
export import rstd.serde;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::collections::BTreeMapIter;
using ::alloc::string::String;

namespace rstd::json
{

class ValueSerializer;
class ValueSequenceSerializer;
class ValueMapSerializer;

class ValueSerializer {
    serde::DataPath path_;

public:
    using value_type    = Value;
    using error_type    = serde::Error;
    using result_type   = Result<Value, serde::Error>;
    using sequence_type = ValueSequenceSerializer;
    using map_type      = ValueMapSerializer;

    ValueSerializer() = default;
    explicit ValueSerializer(serde::DataPath path): path_(rstd::move(path)) {}

    auto serialize_none() -> result_type { return Ok(Value::Null()); }
    auto serialize_bool(bool value) -> result_type { return Ok(Value::Bool(value)); }
    auto serialize_i64(i64 value) -> result_type {
        return Ok(Value::Number(Number::from_i64(value)));
    }
    auto serialize_u64(u64 value) -> result_type {
        return Ok(Value::Number(Number::from_u64(value)));
    }
    auto serialize_f64(f64 value) -> result_type {
        auto number = Number::from_f64(value);
        if (number.is_none()) return Err(invalid_value("JSON numbers must be finite"_str));
        return Ok(Value::Number(rstd::move(number).unwrap_unchecked()));
    }
    auto serialize_string(ref<str> value) -> result_type {
        return Ok(Value::String(String::make(value)));
    }

    template<typename T>
    auto serialize_some(const T& value) -> result_type {
        return serde::serialize(*this, value);
    }

    auto begin_sequence(usize) -> Result<ValueSequenceSerializer, serde::Error>;
    auto begin_map(usize) -> Result<ValueMapSerializer, serde::Error>;
    auto invalid_value(ref<str> message) const -> serde::Error {
        return serde::Error::invalid_value(path_.clone(), message);
    }
    template<typename Source>
    auto invalid_value_with_source(ref<str> message, Source source) const -> serde::Error {
        return serde::Error::invalid_value_with_source(path_.clone(), message, rstd::move(source));
    }
    auto unsupported(ref<str> message) const -> serde::Error {
        return serde::Error::unsupported(path_.clone(), message);
    }

    template<typename T>
    auto serialize_extension(ref<str>, const T&) -> result_type {
        return Err(unsupported("JSON does not support this extension"_str));
    }
};

class ValueSequenceSerializer {
    Array           values_;
    serde::DataPath path_;
    bool            ended_ {};

public:
    using error_type = serde::Error;

    ValueSequenceSerializer(usize len, serde::DataPath path)
        : values_(Array::with_capacity(len)), path_(rstd::move(path)) {}

    template<typename T>
    auto element(const T& value) -> Result<empty, serde::Error> {
        if (ended_) {
            return Err(serde::Error::invariant(path_.clone(), "sequence already ended"_str));
        }
        auto serializer = ValueSerializer(path_.with_index(values_.len()));
        auto encoded    = serde::serialize(serializer, value);
        if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err_unchecked());
        values_.push(rstd::move(encoded).unwrap_unchecked());
        return Ok(empty {});
    }

    auto end() -> Result<Value, serde::Error> {
        if (ended_) {
            return Err(serde::Error::invariant(path_.clone(), "sequence already ended"_str));
        }
        ended_ = true;
        return Ok(Value::Array(rstd::move(values_)));
    }
};

class ValueKeySerializer {
    serde::DataPath path_;

public:
    using value_type  = String;
    using error_type  = serde::Error;
    using result_type = Result<String, serde::Error>;

    explicit ValueKeySerializer(serde::DataPath path): path_(rstd::move(path)) {}

    auto serialize_string(ref<str> value) -> result_type { return Ok(String::make(value)); }
    auto serialize_none() -> result_type { return unsupported(); }
    auto serialize_bool(bool) -> result_type { return unsupported(); }
    auto serialize_i64(i64) -> result_type { return unsupported(); }
    auto serialize_u64(u64) -> result_type { return unsupported(); }
    auto serialize_f64(f64) -> result_type { return unsupported(); }

    template<typename T>
    auto serialize_some(const T&) -> result_type {
        return unsupported();
    }

    template<typename T>
    auto serialize_extension(ref<str>, const T&) -> result_type {
        return unsupported();
    }

private:
    auto unsupported() const -> result_type {
        return Err(serde::Error::unsupported(path_.clone(),
                                             "JSON object keys must serialize as strings"_str));
    }
};

class ValueMapSerializer {
    Map             values_;
    serde::DataPath path_;
    serde::DataPath value_path_;
    Option<String>  pending_key_;
    bool            ended_ {};

public:
    using error_type = serde::Error;

    ValueMapSerializer(serde::DataPath path)
        : values_(Map::make()), path_(path.clone()), value_path_(rstd::move(path)) {}

    template<typename T>
    auto key(const T& value) -> Result<empty, serde::Error> {
        if (ended_) return Err(serde::Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_key_.is_some()) {
            return Err(serde::Error::invariant(path_.clone(), "map value is missing"_str));
        }
        auto serializer = ValueKeySerializer(path_.clone());
        auto encoded    = serde::serialize(serializer, value);
        if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err_unchecked());
        auto key_value = rstd::move(encoded).unwrap_unchecked();
        value_path_    = path_.with_map_key(key_value.as_str());
        pending_key_   = Some(rstd::move(key_value));
        return Ok(empty {});
    }

    template<typename T>
    auto value(const T& value) -> Result<empty, serde::Error> {
        if (ended_) return Err(serde::Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_key_.is_none()) {
            return Err(serde::Error::invariant(path_.clone(), "map key is missing"_str));
        }
        auto serializer = ValueSerializer(value_path_.clone());
        auto encoded    = serde::serialize(serializer, value);
        if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err_unchecked());
        auto key      = rstd::move(pending_key_).unwrap_unchecked();
        auto key_name = key.clone();
        auto replaced = values_.insert(rstd::move(key), rstd::move(encoded).unwrap_unchecked());
        pending_key_  = None();
        if (replaced.is_some()) {
            return Err(serde::Error::duplicate_field(path_.clone(), key_name.as_str()));
        }
        return Ok(empty {});
    }

    auto end() -> Result<Value, serde::Error> {
        if (ended_) return Err(serde::Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_key_.is_some()) {
            return Err(serde::Error::invariant(path_.clone(), "map value is missing"_str));
        }
        ended_ = true;
        return Ok(Value::Object(rstd::move(values_)));
    }
};

inline auto ValueSerializer::begin_sequence(usize len)
    -> Result<ValueSequenceSerializer, serde::Error> {
    return Ok(ValueSequenceSerializer(len, path_.clone()));
}

inline auto ValueSerializer::begin_map(usize) -> Result<ValueMapSerializer, serde::Error> {
    return Ok(ValueMapSerializer(path_.clone()));
}

class ValueDeserializer;
class ValueSequenceAccess;
class ValueMapAccess;

class ValueDeserializer {
    ref<Value>      value_;
    serde::DataPath path_;

    static auto actual_kind(const Value& value) noexcept -> serde::ValueKind {
        if (value.is_null()) return serde::ValueKind::Null;
        if (value.is_boolean()) return serde::ValueKind::Boolean;
        if (value.is_string()) return serde::ValueKind::String;
        if (value.is_array()) return serde::ValueKind::Sequence;
        if (value.is_object()) return serde::ValueKind::Map;
        if (value.is_f64()) return serde::ValueKind::Float;
        if (value.is_u64()) return serde::ValueKind::UnsignedInteger;
        return serde::ValueKind::SignedInteger;
    }

    auto mismatch(serde::ValueKind expected) const -> serde::Error {
        return serde::Error::type_mismatch(path_.clone(), expected, actual_kind(*value_));
    }

    friend class ValueSequenceAccess;
    friend class ValueMapAccess;

public:
    using error_type    = serde::Error;
    using sequence_type = ValueSequenceAccess;
    using map_type      = ValueMapAccess;

    explicit ValueDeserializer(const Value& value)
        : value_(ref<Value>::from_raw_parts(rstd::addressof(value))) {}
    ValueDeserializer(const Value& value, serde::DataPath path)
        : value_(ref<Value>::from_raw_parts(rstd::addressof(value))), path_(rstd::move(path)) {}

    auto deserialize_bool() -> Result<bool, serde::Error> {
        auto value = value_->as_bool();
        if (value.is_none()) return Err(mismatch(serde::ValueKind::Boolean));
        return Ok(*value);
    }
    auto deserialize_i64() -> Result<i64, serde::Error> {
        auto value = value_->as_i64();
        if (value.is_none()) return Err(mismatch(serde::ValueKind::SignedInteger));
        return Ok(*value);
    }
    auto deserialize_u64() -> Result<u64, serde::Error> {
        auto value = value_->as_u64();
        if (value.is_none()) return Err(mismatch(serde::ValueKind::UnsignedInteger));
        return Ok(*value);
    }
    auto deserialize_f64() -> Result<f64, serde::Error> {
        auto value = value_->as_f64();
        if (value.is_none()) return Err(mismatch(serde::ValueKind::Float));
        return Ok(*value);
    }
    auto deserialize_string() -> Result<String, serde::Error> {
        auto value = value_->as_str();
        if (value.is_none()) return Err(mismatch(serde::ValueKind::String));
        return Ok(String::make(*value));
    }

    template<typename T>
    auto deserialize_option() -> Result<Option<T>, serde::Error> {
        if (value_->is_null()) return Ok(None<T>());
        auto child = ValueDeserializer(*value_, path_.clone());
        auto value = serde::deserialize<T>(child);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(Some(rstd::move(value).unwrap_unchecked()));
    }

    auto begin_sequence() -> Result<ValueSequenceAccess, serde::Error>;
    auto begin_map() -> Result<ValueMapAccess, serde::Error>;
    auto invalid_value(ref<str> message) const -> serde::Error {
        return serde::Error::invalid_value(path_.clone(), message);
    }
    template<typename Source>
    auto invalid_value_with_source(ref<str> message, Source source) const -> serde::Error {
        return serde::Error::invalid_value_with_source(path_.clone(), message, rstd::move(source));
    }
    auto missing_field(ref<str> field) const -> serde::Error {
        return serde::Error::missing_field(path_.clone(), field);
    }
    auto unknown_field(ref<str> field) const -> serde::Error {
        return serde::Error::unknown_field(path_.clone(), field);
    }
    auto duplicate_field(ref<str> field) const -> serde::Error {
        return serde::Error::duplicate_field(path_.clone(), field);
    }
    auto unsupported(ref<str> message) const -> serde::Error {
        return serde::Error::unsupported(path_.clone(), message);
    }

    template<typename T>
    auto deserialize_extension(ref<str>) -> Result<T, serde::Error> {
        return Err(unsupported("JSON does not support this extension"_str));
    }
};

class ValueSequenceAccess {
    slice<Value>    values_;
    serde::DataPath path_;
    usize           index_ {};
    bool            ended_ {};

public:
    ValueSequenceAccess(slice<Value> values, serde::DataPath path)
        : values_(values), path_(rstd::move(path)) {}

    template<typename T>
    auto next() -> Result<Option<T>, serde::Error> {
        if (ended_) {
            return Err(serde::Error::invariant(path_.clone(), "sequence already ended"_str));
        }
        if (index_ == values_.len()) return Ok(None<T>());
        auto child = ValueDeserializer(values_[index_], path_.with_index(index_));
        auto value = serde::deserialize<T>(child);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        ++index_;
        return Ok(Some(rstd::move(value).unwrap_unchecked()));
    }

    auto end() -> Result<empty, serde::Error> {
        if (ended_) {
            return Err(serde::Error::invariant(path_.clone(), "sequence already ended"_str));
        }
        if (index_ != values_.len()) {
            return Err(serde::Error::invariant(path_.clone(), "sequence has unread elements"_str));
        }
        ended_ = true;
        return Ok(empty {});
    }
};

class ValueMapAccess {
    BTreeMapIter<String, Value> entries_;
    serde::DataPath             path_;
    serde::DataPath             value_path_;
    Value                       key_value_;
    Option<ref<Value>>          pending_value_;
    bool                        finished_ {};
    bool                        ended_ {};

public:
    ValueMapAccess(const Map& values, serde::DataPath path)
        : entries_(values.iter()), path_(path.clone()), value_path_(rstd::move(path)) {}

    template<typename T>
    auto next_key() -> Result<Option<T>, serde::Error> {
        if (ended_) return Err(serde::Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_value_.is_some()) {
            return Err(serde::Error::invariant(path_.clone(), "map value is missing"_str));
        }
        auto entry = entries_.next();
        if (entry.is_none()) {
            finished_ = true;
            return Ok(None<T>());
        }
        auto [key, value] = *entry;
        key_value_        = Value::String(key->clone());
        value_path_       = path_.with_map_key(key->as_str());
        pending_value_    = Some<ref<Value>>(value);
        auto child        = ValueDeserializer(key_value_, value_path_.clone());
        auto decoded      = serde::deserialize<T>(child);
        if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err_unchecked());
        return Ok(Some(rstd::move(decoded).unwrap_unchecked()));
    }

    template<typename T>
    auto next_value() -> Result<T, serde::Error> {
        if (ended_) return Err(serde::Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_value_.is_none()) {
            return Err(serde::Error::invariant(path_.clone(), "map key is missing"_str));
        }
        auto child   = ValueDeserializer(**pending_value_, value_path_.clone());
        auto decoded = serde::deserialize<T>(child);
        if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err_unchecked());
        pending_value_ = None();
        return decoded;
    }

    auto end() -> Result<empty, serde::Error> {
        if (ended_) return Err(serde::Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_value_.is_some()) {
            return Err(serde::Error::invariant(path_.clone(), "map value is missing"_str));
        }
        if (! finished_ && entries_.next().is_some()) {
            return Err(serde::Error::invariant(path_.clone(), "map has unread entries"_str));
        }
        ended_ = true;
        return Ok(empty {});
    }
};

inline auto ValueDeserializer::begin_sequence() -> Result<ValueSequenceAccess, serde::Error> {
    auto values = value_->as_array();
    if (values.is_none()) return Err(mismatch(serde::ValueKind::Sequence));
    return Ok(ValueSequenceAccess(values->get().as_slice(), path_.clone()));
}

inline auto ValueDeserializer::begin_map() -> Result<ValueMapAccess, serde::Error> {
    auto values = value_->as_object();
    if (values.is_none()) return Err(mismatch(serde::ValueKind::Map));
    return Ok(ValueMapAccess(values->get(), path_.clone()));
}

export class DecodeError {
    Option<Error>        syntax_;
    Option<serde::Error> data_;

    DecodeError(Option<Error> syntax, Option<serde::Error> data)
        : syntax_(rstd::move(syntax)), data_(rstd::move(data)) {}

public:
    static auto syntax(Error error) -> DecodeError {
        return DecodeError(Some(rstd::move(error)), None());
    }
    static auto data(serde::Error error) -> DecodeError {
        return DecodeError(None(), Some(rstd::move(error)));
    }

    auto is_syntax() const noexcept -> bool { return syntax_.is_some(); }
    auto is_data() const noexcept -> bool { return data_.is_some(); }
    auto syntax_error() const noexcept [[clang::lifetimebound]] -> Option<ref<Error>> {
        if (syntax_.is_none()) return None();
        return Some(ref<Error>::from_raw_parts(rstd::addressof(*syntax_)));
    }
    auto data_error() const noexcept [[clang::lifetimebound]] -> Option<ref<serde::Error>> {
        if (data_.is_none()) return None();
        return Some(ref<serde::Error>::from_raw_parts(rstd::addressof(*data_)));
    }
};

export template<typename T>
auto to_value(const T& value) -> Result<Value, serde::Error> {
    auto serializer = ValueSerializer();
    return serde::serialize(serializer, value);
}

export template<typename T>
auto decode_value(const Value& value) -> Result<T, serde::Error> {
    auto deserializer = ValueDeserializer(value);
    return serde::deserialize<T>(deserializer);
}

export template<typename T>
auto encode(const T& value, FormatOptions options = {}) -> Result<String, serde::Error> {
    auto encoded = to_value(value);
    if (encoded.is_err()) return Err(rstd::move(encoded).unwrap_err_unchecked());
    return Ok(to_string(encoded.unwrap_unchecked(), options));
}

export template<typename T>
auto decode(ref<str> input, ParseOptions options = {}) -> Result<T, DecodeError> {
    auto parsed = from_str(input, options);
    if (parsed.is_err()) return Err(DecodeError::syntax(rstd::move(parsed).unwrap_err_unchecked()));
    auto decoded = decode_value<T>(rstd::move(parsed).unwrap_unchecked());
    if (decoded.is_err()) {
        return Err(DecodeError::data(rstd::move(decoded).unwrap_err_unchecked()));
    }
    return Ok(rstd::move(decoded).unwrap_unchecked());
}

} // namespace rstd::json

namespace rstd
{

template<>
struct Impl<fmt::Display, json::DecodeError> : ImplBase<json::DecodeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto syntax = this->self().syntax_error();
        if (syntax.is_some()) return as<fmt::Display>(syntax->get()).fmt(formatter);
        return as<fmt::Display>(this->self().data_error()->get()).fmt(formatter);
    }
};

template<>
struct Impl<fmt::Debug, json::DecodeError> : ImplBase<json::DecodeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, json::DecodeError> : ImplBase<json::DecodeError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        auto syntax = this->self().syntax_error();
        if (syntax.is_some()) return Some(dyn<error::Error>::from_ref(syntax->get()));
        return Some(dyn<error::Error>::from_ref(this->self().data_error()->get()));
    }
};

} // namespace rstd

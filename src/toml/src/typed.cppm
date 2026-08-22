export module rstd.toml:typed;
export import :parser;
export import :serialize;
export import rstd.serde;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::collections::BTreeMapIter;
using ::alloc::string::String;

namespace rstd::toml
{

class ValueSerializer;
class ValueSequenceSerializer;
class ValueMapSerializer;
class ValueEnumAccess;

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

    auto serialize_none() -> result_type {
        return Err(serde::Error::unsupported(path_.clone(), "TOML has no null representation"_str));
    }
    auto serialize_unit() -> result_type {
        return Err(serde::Error::unsupported(path_.clone(), "TOML has no unit representation"_str));
    }
    auto serialize_bool(bool value) -> result_type { return Ok(Value::Boolean(value)); }
    auto serialize_i64(i64 value) -> result_type { return Ok(Value::Integer(value)); }
    auto serialize_u64(u64 value) -> result_type {
        if (value > rstd::as_cast<u64>(i64::MAX)) {
            return Err(invalid_value("unsigned integer does not fit TOML integer"_str));
        }
        return Ok(Value::Integer(rstd::as_cast<i64>(value)));
    }
    auto serialize_f64(f64 value) -> result_type { return Ok(Value::Float(value)); }
    auto serialize_string(ref<str> value) -> result_type {
        return Ok(Value::String(String::make(value)));
    }
    auto serialize_bytes(slice<u8> value) -> result_type {
        auto bytes = Array::with_capacity(value.len());
        for (auto byte : value) bytes.push(Value::Integer(rstd::as_cast<i64>(byte)));
        return Ok(Value::Array(rstd::move(bytes)));
    }

    template<typename T>
    auto serialize_some(const T& value) -> result_type {
        return serde::serialize(*this, value);
    }

    template<typename T>
    auto serialize_newtype(ref<str>, const T& value) -> result_type {
        return serde::serialize(*this, value);
    }

    auto serialize_unit_variant(ref<str> variant) -> result_type {
        return Ok(Value::String(String::make(variant)));
    }

    template<typename T>
    auto serialize_newtype_variant(ref<str> variant, const T& value) -> result_type {
        auto serializer = ValueSerializer(path_.with_variant(variant));
        auto encoded    = serde::serialize(serializer, value);
        if (encoded.is_err()) return encoded;
        auto table = Table::make();
        table.insert(String::make(variant), rstd::move(encoded).unwrap_unchecked());
        return Ok(Value::Table(rstd::move(table)));
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

    auto serialize_extension(ref<str> name, LocalDate value) -> result_type {
        if (name != "toml.local-date"_str) return Err(unsupported("unknown TOML extension"_str));
        return Ok(Value::LocalDate(value));
    }
    auto serialize_extension(ref<str> name, LocalTime value) -> result_type {
        if (name != "toml.local-time"_str) return Err(unsupported("unknown TOML extension"_str));
        return Ok(Value::LocalTime(value));
    }
    auto serialize_extension(ref<str> name, LocalDateTime value) -> result_type {
        if (name != "toml.local-date-time"_str) {
            return Err(unsupported("unknown TOML extension"_str));
        }
        return Ok(Value::LocalDateTime(value));
    }
    auto serialize_extension(ref<str> name, OffsetDateTime value) -> result_type {
        if (name != "toml.offset-date-time"_str) {
            return Err(unsupported("unknown TOML extension"_str));
        }
        return Ok(Value::OffsetDateTime(value));
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
                                             "TOML table keys must serialize as strings"_str));
    }
};

class ValueMapSerializer {
    Table           values_;
    serde::DataPath path_;
    serde::DataPath value_path_;
    Option<String>  pending_key_;
    bool            ended_ {};

public:
    using error_type = serde::Error;

    ValueMapSerializer(serde::DataPath path)
        : values_(Table::make()), path_(path.clone()), value_path_(rstd::move(path)) {}

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
        return Ok(Value::Table(rstd::move(values_)));
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
        if (value.is_boolean()) return serde::ValueKind::Boolean;
        if (value.is_string()) return serde::ValueKind::String;
        if (value.is_array()) return serde::ValueKind::Sequence;
        if (value.is_table()) return serde::ValueKind::Map;
        if (value.is_float()) return serde::ValueKind::Float;
        if (value.is_integer()) return serde::ValueKind::SignedInteger;
        return serde::ValueKind::Extension;
    }

    auto mismatch(serde::ValueKind expected) const -> serde::Error {
        return serde::Error::type_mismatch(path_.clone(), expected, actual_kind(*value_));
    }

    friend class ValueSequenceAccess;
    friend class ValueMapAccess;
    friend class ValueEnumAccess;

public:
    using error_type    = serde::Error;
    using sequence_type = ValueSequenceAccess;
    using map_type      = ValueMapAccess;
    using enum_type     = ValueEnumAccess;

    explicit ValueDeserializer(const Value& value)
        : value_(ref<Value>::from_raw_parts(rstd::addressof(value))) {}
    ValueDeserializer(const Value& value, serde::DataPath path)
        : value_(ref<Value>::from_raw_parts(rstd::addressof(value))), path_(rstd::move(path)) {}

    auto deserialize_bool() -> Result<bool, serde::Error> {
        auto value = value_->as_bool();
        if (value.is_none()) return Err(mismatch(serde::ValueKind::Boolean));
        return Ok(*value);
    }
    auto deserialize_unit() -> Result<empty, serde::Error> {
        return Err(mismatch(serde::ValueKind::Unit));
    }
    auto deserialize_i64() -> Result<i64, serde::Error> {
        auto value = value_->as_integer();
        if (value.is_none()) return Err(mismatch(serde::ValueKind::SignedInteger));
        return Ok(*value);
    }
    auto deserialize_u64() -> Result<u64, serde::Error> {
        auto value = value_->as_integer();
        if (value.is_none()) return Err(mismatch(serde::ValueKind::UnsignedInteger));
        if (*value < i64()) return Err(invalid_value("negative TOML integer is not unsigned"_str));
        return Ok(rstd::as_cast<u64>(*value));
    }
    auto deserialize_f64() -> Result<f64, serde::Error> {
        auto value = value_->as_float();
        if (value.is_none()) return Err(mismatch(serde::ValueKind::Float));
        return Ok(*value);
    }
    auto deserialize_string() -> Result<String, serde::Error> {
        auto value = value_->as_str();
        if (value.is_none()) return Err(mismatch(serde::ValueKind::String));
        return Ok(String::make(*value));
    }
    auto deserialize_bytes() -> Result<::alloc::vec::Vec<u8>, serde::Error> {
        auto values = value_->as_array();
        if (values.is_none()) return Err(mismatch(serde::ValueKind::Bytes));
        auto bytes = ::alloc::vec::Vec<u8>::make();
        for (usize index {}; index < values->get().len(); ++index) {
            auto child = ValueDeserializer(values->get()[index], path_.with_index(index));
            auto byte  = serde::deserialize<u8>(child);
            if (byte.is_err()) return Err(rstd::move(byte).unwrap_err_unchecked());
            bytes.push(rstd::move(byte).unwrap_unchecked());
        }
        return Ok(rstd::move(bytes));
    }

    template<typename T>
    auto deserialize_option() -> Result<Option<T>, serde::Error> {
        auto child = ValueDeserializer(*value_, path_.clone());
        auto value = serde::deserialize<T>(child);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(Some(rstd::move(value).unwrap_unchecked()));
    }

    template<typename T>
    auto deserialize_newtype(ref<str>) -> Result<T, serde::Error> {
        return serde::deserialize<T>(*this);
    }

    auto begin_sequence() -> Result<ValueSequenceAccess, serde::Error>;
    auto begin_map() -> Result<ValueMapAccess, serde::Error>;
    auto begin_enum() -> Result<ValueEnumAccess, serde::Error>;
    auto ignore_value() -> Result<empty, serde::Error> { return Ok(empty {}); }
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
    auto unknown_variant(ref<str> variant) const -> serde::Error {
        return serde::Error::unknown_variant(path_.clone(), variant);
    }
    auto duplicate_field(ref<str> field) const -> serde::Error {
        return serde::Error::duplicate_field(path_.clone(), field);
    }
    auto invariant(ref<str> message) const -> serde::Error {
        return serde::Error::invariant(path_.clone(), message);
    }
    auto unsupported(ref<str> message) const -> serde::Error {
        return serde::Error::unsupported(path_.clone(), message);
    }

    template<typename T>
    auto deserialize_extension(ref<str> name) -> Result<T, serde::Error> {
        if constexpr (mtp::same_as<T, LocalDate>) {
            if (name != "toml.local-date"_str) {
                return Err(unsupported("unknown TOML extension"_str));
            }
            auto value = value_->as_local_date();
            if (value.is_none()) return Err(mismatch(serde::ValueKind::Extension));
            return Ok(*value);
        } else if constexpr (mtp::same_as<T, LocalTime>) {
            if (name != "toml.local-time"_str) {
                return Err(unsupported("unknown TOML extension"_str));
            }
            auto value = value_->as_local_time();
            if (value.is_none()) return Err(mismatch(serde::ValueKind::Extension));
            return Ok(*value);
        } else if constexpr (mtp::same_as<T, LocalDateTime>) {
            if (name != "toml.local-date-time"_str) {
                return Err(unsupported("unknown TOML extension"_str));
            }
            auto value = value_->as_local_datetime();
            if (value.is_none()) return Err(mismatch(serde::ValueKind::Extension));
            return Ok(*value);
        } else if constexpr (mtp::same_as<T, OffsetDateTime>) {
            if (name != "toml.offset-date-time"_str) {
                return Err(unsupported("unknown TOML extension"_str));
            }
            auto value = value_->as_offset_datetime();
            if (value.is_none()) return Err(mismatch(serde::ValueKind::Extension));
            return Ok(*value);
        } else {
            return Err(unsupported("unknown TOML extension type"_str));
        }
    }
};

class ValueSequenceAccess {
    slice<Value>    values_;
    serde::DataPath path_;
    usize           index_ {};
    bool            ended_ {};

public:
    using error_type = serde::Error;

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
    using error_type = serde::Error;

    ValueMapAccess(const Table& values, serde::DataPath path)
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

    auto ignore_value() -> Result<empty, serde::Error> {
        if (ended_) return Err(serde::Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_value_.is_none()) {
            return Err(serde::Error::invariant(path_.clone(), "map key is missing"_str));
        }
        pending_value_ = None();
        return Ok(empty {});
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

class ValueEnumAccess {
    String             variant_;
    Option<ref<Value>> value_;
    serde::DataPath    path_;
    bool               consumed_ {};

public:
    using error_type = serde::Error;

    ValueEnumAccess(String variant, Option<ref<Value>> value, serde::DataPath path)
        : variant_(rstd::move(variant)), value_(value), path_(rstd::move(path)) {}

    auto variant() const noexcept [[clang::lifetimebound]] -> ref<str> { return variant_.as_str(); }

    auto unit() -> Result<empty, serde::Error> {
        if (consumed_) {
            return Err(serde::Error::invariant(path_.clone(), "enum already consumed"_str));
        }
        if (value_.is_some()) {
            return Err(serde::Error::invalid_value(path_.with_variant(variant_.as_str()),
                                                   "enum variant has an unexpected payload"_str));
        }
        consumed_ = true;
        return Ok(empty {});
    }

    template<typename T>
    auto value() -> Result<T, serde::Error> {
        if (consumed_) {
            return Err(serde::Error::invariant(path_.clone(), "enum already consumed"_str));
        }
        if (value_.is_none()) {
            return Err(serde::Error::invalid_value(path_.with_variant(variant_.as_str()),
                                                   "enum variant payload is missing"_str));
        }
        consumed_  = true;
        auto child = ValueDeserializer(**value_, path_.with_variant(variant_.as_str()));
        return serde::deserialize<T>(child);
    }
};

inline auto ValueDeserializer::begin_sequence() -> Result<ValueSequenceAccess, serde::Error> {
    auto values = value_->as_array();
    if (values.is_none()) return Err(mismatch(serde::ValueKind::Sequence));
    return Ok(ValueSequenceAccess(values->get().as_slice(), path_.clone()));
}

inline auto ValueDeserializer::begin_map() -> Result<ValueMapAccess, serde::Error> {
    auto values = value_->as_table();
    if (values.is_none()) return Err(mismatch(serde::ValueKind::Map));
    return Ok(ValueMapAccess(values->get(), path_.clone()));
}

inline auto ValueDeserializer::begin_enum() -> Result<ValueEnumAccess, serde::Error> {
    auto name = value_->as_str();
    if (name.is_some()) {
        return Ok(ValueEnumAccess(String::make(*name), None(), path_.clone()));
    }
    auto values = value_->as_table();
    if (values.is_none()) return Err(mismatch(serde::ValueKind::Enum));
    auto entries = values->get().iter();
    auto entry   = entries.next();
    if (entry.is_none() || entries.next().is_some()) {
        return Err(invalid_value("externally tagged enum must contain exactly one variant"_str));
    }
    auto [variant, payload] = *entry;
    return Ok(ValueEnumAccess(variant->clone(), Some<ref<Value>>(payload), path_.clone()));
}

static_assert(serde::Serializer<ValueSerializer>);
static_assert(serde::Deserializer<ValueDeserializer>);

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

export class EncodeError {
    Option<serde::Error>   data_;
    Option<SerializeError> format_;

    EncodeError(Option<serde::Error> data, Option<SerializeError> format)
        : data_(rstd::move(data)), format_(rstd::move(format)) {}

public:
    static auto data(serde::Error error) -> EncodeError {
        return EncodeError(Some(rstd::move(error)), None());
    }
    static auto format(SerializeError error) -> EncodeError {
        return EncodeError(None(), Some(rstd::move(error)));
    }

    auto data_error() const noexcept [[clang::lifetimebound]] -> Option<ref<serde::Error>> {
        if (data_.is_none()) return None();
        return Some(ref<serde::Error>::from_raw_parts(rstd::addressof(*data_)));
    }
    auto format_error() const noexcept [[clang::lifetimebound]] -> Option<ref<SerializeError>> {
        if (format_.is_none()) return None();
        return Some(ref<SerializeError>::from_raw_parts(rstd::addressof(*format_)));
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
auto decode_value(const Value& value, serde::DataPath path) -> Result<T, serde::Error> {
    auto deserializer = ValueDeserializer(value, rstd::move(path));
    return serde::deserialize<T>(deserializer);
}

export template<typename T>
auto encode(const T& value) -> Result<String, EncodeError> {
    auto encoded = to_value(value);
    if (encoded.is_err()) {
        return Err(EncodeError::data(rstd::move(encoded).unwrap_err_unchecked()));
    }
    auto text = to_string(encoded.unwrap_unchecked());
    if (text.is_err()) return Err(EncodeError::format(rstd::move(text).unwrap_err_unchecked()));
    return Ok(rstd::move(text).unwrap_unchecked());
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

} // namespace rstd::toml

export namespace rstd
{

template<>
struct Impl<serde::Serialize, toml::LocalDate> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, toml::LocalDate value) {
        return serializer.serialize_extension("toml.local-date"_str, value);
    }
};

template<>
struct Impl<serde::Deserialize, toml::LocalDate> {
    template<typename Deserializer>
    static decltype(auto) deserialize(Deserializer& deserializer) {
        return deserializer.template deserialize_extension<toml::LocalDate>("toml.local-date"_str);
    }
};

template<>
struct Impl<serde::Serialize, toml::LocalTime> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, toml::LocalTime value) {
        return serializer.serialize_extension("toml.local-time"_str, value);
    }
};

template<>
struct Impl<serde::Deserialize, toml::LocalTime> {
    template<typename Deserializer>
    static decltype(auto) deserialize(Deserializer& deserializer) {
        return deserializer.template deserialize_extension<toml::LocalTime>("toml.local-time"_str);
    }
};

template<>
struct Impl<serde::Serialize, toml::LocalDateTime> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, toml::LocalDateTime value) {
        return serializer.serialize_extension("toml.local-date-time"_str, value);
    }
};

template<>
struct Impl<serde::Deserialize, toml::LocalDateTime> {
    template<typename Deserializer>
    static decltype(auto) deserialize(Deserializer& deserializer) {
        return deserializer.template deserialize_extension<toml::LocalDateTime>(
            "toml.local-date-time"_str);
    }
};

template<>
struct Impl<serde::Serialize, toml::OffsetDateTime> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, toml::OffsetDateTime value) {
        return serializer.serialize_extension("toml.offset-date-time"_str, value);
    }
};

template<>
struct Impl<serde::Deserialize, toml::OffsetDateTime> {
    template<typename Deserializer>
    static decltype(auto) deserialize(Deserializer& deserializer) {
        return deserializer.template deserialize_extension<toml::OffsetDateTime>(
            "toml.offset-date-time"_str);
    }
};

} // namespace rstd

namespace rstd
{

template<>
struct Impl<fmt::Display, toml::DecodeError> : ImplBase<toml::DecodeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto syntax = this->self().syntax_error();
        if (syntax.is_some()) return as<fmt::Display>(syntax->get()).fmt(formatter);
        return as<fmt::Display>(this->self().data_error()->get()).fmt(formatter);
    }
};

template<>
struct Impl<fmt::Debug, toml::DecodeError> : ImplBase<toml::DecodeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, toml::DecodeError> : ImplBase<toml::DecodeError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        auto syntax = this->self().syntax_error();
        if (syntax.is_some()) return Some(dyn<error::Error>::from_ref(syntax->get()));
        return Some(dyn<error::Error>::from_ref(this->self().data_error()->get()));
    }
};

template<>
struct Impl<fmt::Display, toml::EncodeError> : ImplBase<toml::EncodeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        auto data = this->self().data_error();
        if (data.is_some()) return as<fmt::Display>(data->get()).fmt(formatter);
        return as<fmt::Display>(this->self().format_error()->get()).fmt(formatter);
    }
};

template<>
struct Impl<fmt::Debug, toml::EncodeError> : ImplBase<toml::EncodeError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct Impl<error::Error, toml::EncodeError> : ImplBase<toml::EncodeError> {
    auto source() const noexcept -> Option<error::ErrorRef> {
        auto data = this->self().data_error();
        if (data.is_some()) return Some(dyn<error::Error>::from_ref(data->get()));
        return Some(dyn<error::Error>::from_ref(this->self().format_error()->get()));
    }
};

} // namespace rstd

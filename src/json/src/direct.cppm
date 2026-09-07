export module rstd.json:direct;
import :reader;
export import rstd.serde;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::collections::BTreeMap;
using ::alloc::string::String;
using ::alloc::vec::Vec;

namespace rstd::json
{

class DirectDeserializer;
class DirectSequenceAccess;
class DirectMapAccess;
class DirectEnumAccess;

auto direct_syntax_error(serde::DataPath path, Error source) -> serde::Error {
    return serde::Error::invalid_value_with_source(
        rstd::move(path), "invalid JSON syntax"_str, rstd::move(source));
}

auto direct_value_kind(JsonValueKind kind) noexcept -> serde::ValueKind {
    switch (kind) {
    case JsonValueKind::Null: return serde::ValueKind::Null;
    case JsonValueKind::Boolean: return serde::ValueKind::Boolean;
    case JsonValueKind::Number: return serde::ValueKind::Float;
    case JsonValueKind::String: return serde::ValueKind::String;
    case JsonValueKind::Array: return serde::ValueKind::Sequence;
    case JsonValueKind::Object: return serde::ValueKind::Map;
    }
    rstd::unreachable();
}

class DirectDeserializer {
    JsonReader*     reader_ {};
    serde::DataPath path_;

    auto mismatch(serde::ValueKind expected) -> serde::Error {
        auto actual = reader_->value_kind();
        if (actual.is_err()) {
            return direct_syntax_error(path_.clone(), rstd::move(actual).unwrap_err_unchecked());
        }
        return serde::Error::type_mismatch(path_.clone(), expected, direct_value_kind(*actual));
    }

    template<typename T>
    auto syntax(Result<T, Error> result) -> Result<T, serde::Error> {
        if (result.is_err()) {
            return Err(
                direct_syntax_error(path_.clone(), rstd::move(result).unwrap_err_unchecked()));
        }
        return Ok(rstd::move(result).unwrap_unchecked());
    }

    friend class DirectSequenceAccess;
    friend class DirectMapAccess;
    friend class DirectEnumAccess;

public:
    using error_type    = serde::Error;
    using sequence_type = DirectSequenceAccess;
    using map_type      = DirectMapAccess;
    using enum_type     = DirectEnumAccess;

    explicit DirectDeserializer(JsonReader& reader): reader_(rstd::addressof(reader)) {}
    DirectDeserializer(JsonReader& reader, serde::DataPath path)
        : reader_(rstd::addressof(reader)), path_(rstd::move(path)) {}

    auto deserialize_unit() -> Result<empty, serde::Error> {
        auto kind = reader_->value_kind();
        if (kind.is_err()) {
            return Err(direct_syntax_error(path_.clone(), rstd::move(kind).unwrap_err_unchecked()));
        }
        if (*kind != JsonValueKind::Null) return Err(mismatch(serde::ValueKind::Unit));
        return syntax(reader_->parse_null());
    }

    auto deserialize_bool() -> Result<bool, serde::Error> {
        auto kind = reader_->value_kind();
        if (kind.is_err()) {
            return Err(direct_syntax_error(path_.clone(), rstd::move(kind).unwrap_err_unchecked()));
        }
        if (*kind != JsonValueKind::Boolean) return Err(mismatch(serde::ValueKind::Boolean));
        return syntax(reader_->parse_bool());
    }

    auto deserialize_i64() -> Result<i64, serde::Error> {
        auto kind = reader_->value_kind();
        if (kind.is_err()) {
            return Err(direct_syntax_error(path_.clone(), rstd::move(kind).unwrap_err_unchecked()));
        }
        if (*kind != JsonValueKind::Number) return Err(mismatch(serde::ValueKind::SignedInteger));
        auto number = syntax(reader_->parse_number());
        if (number.is_err()) return Err(rstd::move(number).unwrap_err_unchecked());
        auto value = number->as_i64();
        if (value.is_none()) {
            return Err(serde::Error::type_mismatch(
                path_.clone(),
                serde::ValueKind::SignedInteger,
                number->is_f64() ? serde::ValueKind::Float : serde::ValueKind::UnsignedInteger));
        }
        return Ok(*value);
    }

    auto deserialize_u64() -> Result<u64, serde::Error> {
        auto kind = reader_->value_kind();
        if (kind.is_err()) {
            return Err(direct_syntax_error(path_.clone(), rstd::move(kind).unwrap_err_unchecked()));
        }
        if (*kind != JsonValueKind::Number) return Err(mismatch(serde::ValueKind::UnsignedInteger));
        auto number = syntax(reader_->parse_number());
        if (number.is_err()) return Err(rstd::move(number).unwrap_err_unchecked());
        auto value = number->as_u64();
        if (value.is_none()) {
            return Err(serde::Error::type_mismatch(
                path_.clone(),
                serde::ValueKind::UnsignedInteger,
                number->is_f64() ? serde::ValueKind::Float : serde::ValueKind::SignedInteger));
        }
        return Ok(*value);
    }

    auto deserialize_f64() -> Result<f64, serde::Error> {
        auto kind = reader_->value_kind();
        if (kind.is_err()) {
            return Err(direct_syntax_error(path_.clone(), rstd::move(kind).unwrap_err_unchecked()));
        }
        if (*kind != JsonValueKind::Number) return Err(mismatch(serde::ValueKind::Float));
        auto number = syntax(reader_->parse_number());
        if (number.is_err()) return Err(rstd::move(number).unwrap_err_unchecked());
        return Ok(*number->as_f64());
    }

    auto deserialize_string() -> Result<String, serde::Error> {
        auto kind = reader_->value_kind();
        if (kind.is_err()) {
            return Err(direct_syntax_error(path_.clone(), rstd::move(kind).unwrap_err_unchecked()));
        }
        if (*kind != JsonValueKind::String) return Err(mismatch(serde::ValueKind::String));
        return syntax(reader_->parse_string());
    }

    auto deserialize_bytes() -> Result<Vec<u8>, serde::Error>;

    template<typename T>
    auto deserialize_option() -> Result<Option<T>, serde::Error> {
        auto kind = reader_->value_kind();
        if (kind.is_err()) {
            return Err(direct_syntax_error(path_.clone(), rstd::move(kind).unwrap_err_unchecked()));
        }
        if (*kind == JsonValueKind::Null) {
            auto parsed = syntax(reader_->parse_null());
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err_unchecked());
            return Ok(None<T>());
        }
        auto child = DirectDeserializer(*reader_, path_.clone());
        auto value = serde::deserialize<T>(child);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(Some(rstd::move(value).unwrap_unchecked()));
    }

    template<typename T>
    auto deserialize_newtype(ref<str>) -> Result<T, serde::Error> {
        return serde::deserialize<T>(*this);
    }

    auto begin_sequence() -> Result<DirectSequenceAccess, serde::Error>;
    auto begin_map() -> Result<DirectMapAccess, serde::Error>;
    auto begin_enum() -> Result<DirectEnumAccess, serde::Error>;

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
    auto ignore_value() -> Result<empty, serde::Error> { return syntax(reader_->skip_value()); }

    template<typename T>
    auto deserialize_extension(ref<str>) -> Result<T, serde::Error> {
        return Err(unsupported("JSON does not support this extension"_str));
    }
};

class DirectSequenceAccess {
    JsonReader*     reader_ {};
    serde::DataPath path_;
    usize           index_ {};
    bool            first_ { true };
    bool            finished_ {};
    bool            ended_ {};

public:
    using error_type = serde::Error;

    DirectSequenceAccess(JsonReader& reader, serde::DataPath path)
        : reader_(rstd::addressof(reader)), path_(rstd::move(path)) {}

    template<typename T>
    auto next() -> Result<Option<T>, serde::Error> {
        if (ended_)
            return Err(serde::Error::invariant(path_.clone(), "sequence already ended"_str));
        auto next = reader_->next_array(first_);
        if (next.is_err()) {
            return Err(direct_syntax_error(path_.clone(), rstd::move(next).unwrap_err_unchecked()));
        }
        if (! *next) {
            finished_ = true;
            return Ok(None<T>());
        }
        auto child = DirectDeserializer(*reader_, path_.with_index(index_));
        auto value = serde::deserialize<T>(child);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        ++index_;
        return Ok(Some(rstd::move(value).unwrap_unchecked()));
    }

    auto end() -> Result<empty, serde::Error> {
        if (ended_)
            return Err(serde::Error::invariant(path_.clone(), "sequence already ended"_str));
        if (! finished_) {
            return Err(serde::Error::invariant(path_.clone(), "sequence has unread elements"_str));
        }
        ended_ = true;
        return Ok(empty {});
    }
};

class MapKeyDeserializer {
    String          value_;
    serde::DataPath path_;

    auto mismatch(serde::ValueKind expected) const -> serde::Error {
        return serde::Error::type_mismatch(path_.clone(), expected, serde::ValueKind::String);
    }

public:
    using error_type = serde::Error;

    MapKeyDeserializer(String value, serde::DataPath path)
        : value_(rstd::move(value)), path_(rstd::move(path)) {}

    auto deserialize_string() -> Result<String, serde::Error> { return Ok(rstd::move(value_)); }
    auto deserialize_bool() -> Result<bool, serde::Error> {
        return Err(mismatch(serde::ValueKind::Boolean));
    }
    auto deserialize_i64() -> Result<i64, serde::Error> {
        return Err(mismatch(serde::ValueKind::SignedInteger));
    }
    auto deserialize_u64() -> Result<u64, serde::Error> {
        return Err(mismatch(serde::ValueKind::UnsignedInteger));
    }
    auto deserialize_f64() -> Result<f64, serde::Error> {
        return Err(mismatch(serde::ValueKind::Float));
    }
};

class DirectMapAccess {
    JsonReader*             reader_ {};
    serde::DataPath         path_;
    serde::DataPath         value_path_;
    BTreeMap<String, empty> seen_;
    bool                    first_ { true };
    bool                    pending_ {};
    bool                    finished_ {};
    bool                    ended_ {};

public:
    using error_type = serde::Error;

    DirectMapAccess(JsonReader& reader, serde::DataPath path)
        : reader_(rstd::addressof(reader)),
          path_(path.clone()),
          value_path_(rstd::move(path)),
          seen_(BTreeMap<String, empty>::make()) {}

    template<typename T>
    auto next_key() -> Result<Option<T>, serde::Error> {
        if (ended_) return Err(serde::Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_)
            return Err(serde::Error::invariant(path_.clone(), "map value is missing"_str));
        auto key = reader_->next_object_key(first_);
        if (key.is_err()) {
            return Err(direct_syntax_error(path_.clone(), rstd::move(key).unwrap_err_unchecked()));
        }
        auto optional = rstd::move(key).unwrap_unchecked();
        if (optional.is_none()) {
            finished_ = true;
            return Ok(None<T>());
        }
        auto name = rstd::move(optional).unwrap_unchecked();
        if (reader_->reject_duplicate_keys()) {
            if (seen_.contains_key(name.as_str())) {
                return Err(serde::Error::duplicate_field(path_.clone(), name.as_str()));
            }
            seen_.insert(name.clone(), empty {});
        }
        value_path_ = path_.with_map_key(name.as_str());
        pending_    = true;
        auto child  = MapKeyDeserializer(rstd::move(name), value_path_.clone());
        auto value  = serde::deserialize<T>(child);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(Some(rstd::move(value).unwrap_unchecked()));
    }

    template<typename T>
    auto next_value() -> Result<T, serde::Error> {
        if (ended_) return Err(serde::Error::invariant(path_.clone(), "map already ended"_str));
        if (! pending_)
            return Err(serde::Error::invariant(path_.clone(), "map key is missing"_str));
        auto child = DirectDeserializer(*reader_, value_path_.clone());
        auto value = serde::deserialize<T>(child);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        pending_ = false;
        return value;
    }

    auto ignore_value() -> Result<empty, serde::Error> {
        if (ended_) return Err(serde::Error::invariant(path_.clone(), "map already ended"_str));
        if (! pending_)
            return Err(serde::Error::invariant(path_.clone(), "map key is missing"_str));
        auto skipped = reader_->skip_value();
        if (skipped.is_err()) {
            return Err(direct_syntax_error(value_path_.clone(),
                                           rstd::move(skipped).unwrap_err_unchecked()));
        }
        pending_ = false;
        return Ok(empty {});
    }

    auto end() -> Result<empty, serde::Error> {
        if (ended_) return Err(serde::Error::invariant(path_.clone(), "map already ended"_str));
        if (pending_)
            return Err(serde::Error::invariant(path_.clone(), "map value is missing"_str));
        if (! finished_)
            return Err(serde::Error::invariant(path_.clone(), "map has unread entries"_str));
        ended_ = true;
        return Ok(empty {});
    }
};

class DirectEnumAccess {
    JsonReader*     reader_ {};
    String          variant_;
    serde::DataPath path_;
    bool            has_value_ {};
    bool            object_first_ {};
    bool            consumed_ {};

public:
    using error_type = serde::Error;

    DirectEnumAccess(JsonReader&     reader,
                     String          variant,
                     serde::DataPath path,
                     bool            has_value,
                     bool            object_first)
        : reader_(rstd::addressof(reader)),
          variant_(rstd::move(variant)),
          path_(rstd::move(path)),
          has_value_(has_value),
          object_first_(object_first) {}

    auto variant() const noexcept [[clang::lifetimebound]] -> ref<str> { return variant_.as_str(); }

    auto unit() -> Result<empty, serde::Error> {
        if (consumed_)
            return Err(serde::Error::invariant(path_.clone(), "enum already consumed"_str));
        if (has_value_) {
            return Err(serde::Error::invalid_value(path_.with_variant(variant_.as_str()),
                                                   "enum variant has an unexpected payload"_str));
        }
        consumed_ = true;
        return Ok(empty {});
    }

    template<typename T>
    auto value() -> Result<T, serde::Error> {
        if (consumed_)
            return Err(serde::Error::invariant(path_.clone(), "enum already consumed"_str));
        if (! has_value_) {
            return Err(serde::Error::invalid_value(path_.with_variant(variant_.as_str()),
                                                   "enum variant payload is missing"_str));
        }
        auto child = DirectDeserializer(*reader_, path_.with_variant(variant_.as_str()));
        auto value = serde::deserialize<T>(child);
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        auto next = reader_->next_object_key(object_first_);
        if (next.is_err()) {
            return Err(direct_syntax_error(path_.clone(), rstd::move(next).unwrap_err_unchecked()));
        }
        if (next->is_some()) {
            return Err(serde::Error::invalid_value(
                path_.clone(), "externally tagged enum must contain exactly one variant"_str));
        }
        consumed_ = true;
        return value;
    }
};

inline auto DirectDeserializer::begin_sequence() -> Result<DirectSequenceAccess, serde::Error> {
    auto kind = reader_->value_kind();
    if (kind.is_err()) {
        return Err(direct_syntax_error(path_.clone(), rstd::move(kind).unwrap_err_unchecked()));
    }
    if (*kind != JsonValueKind::Array) return Err(mismatch(serde::ValueKind::Sequence));
    auto begun = reader_->begin_array();
    if (begun.is_err()) {
        return Err(direct_syntax_error(path_.clone(), rstd::move(begun).unwrap_err_unchecked()));
    }
    return Ok(DirectSequenceAccess(*reader_, path_.clone()));
}

inline auto DirectDeserializer::begin_map() -> Result<DirectMapAccess, serde::Error> {
    auto kind = reader_->value_kind();
    if (kind.is_err()) {
        return Err(direct_syntax_error(path_.clone(), rstd::move(kind).unwrap_err_unchecked()));
    }
    if (*kind != JsonValueKind::Object) return Err(mismatch(serde::ValueKind::Map));
    auto begun = reader_->begin_object();
    if (begun.is_err()) {
        return Err(direct_syntax_error(path_.clone(), rstd::move(begun).unwrap_err_unchecked()));
    }
    return Ok(DirectMapAccess(*reader_, path_.clone()));
}

inline auto DirectDeserializer::begin_enum() -> Result<DirectEnumAccess, serde::Error> {
    auto kind = reader_->value_kind();
    if (kind.is_err()) {
        return Err(direct_syntax_error(path_.clone(), rstd::move(kind).unwrap_err_unchecked()));
    }
    if (*kind == JsonValueKind::String) {
        auto variant = reader_->parse_string();
        if (variant.is_err()) {
            return Err(
                direct_syntax_error(path_.clone(), rstd::move(variant).unwrap_err_unchecked()));
        }
        return Ok(DirectEnumAccess(
            *reader_, rstd::move(variant).unwrap_unchecked(), path_.clone(), false, false));
    }
    if (*kind != JsonValueKind::Object) return Err(mismatch(serde::ValueKind::Enum));
    auto begun = reader_->begin_object();
    if (begun.is_err()) {
        return Err(direct_syntax_error(path_.clone(), rstd::move(begun).unwrap_err_unchecked()));
    }
    bool first = true;
    auto key   = reader_->next_object_key(first);
    if (key.is_err()) {
        return Err(direct_syntax_error(path_.clone(), rstd::move(key).unwrap_err_unchecked()));
    }
    if (key->is_none()) {
        return Err(invalid_value("externally tagged enum must contain exactly one variant"_str));
    }
    return Ok(DirectEnumAccess(*reader_,
                               rstd::move(key).unwrap_unchecked().unwrap_unchecked(),
                               path_.clone(),
                               true,
                               first));
}

inline auto DirectDeserializer::deserialize_bytes() -> Result<Vec<u8>, serde::Error> {
    auto sequence = begin_sequence();
    if (sequence.is_err()) return Err(rstd::move(sequence).unwrap_err_unchecked());
    auto input = rstd::move(sequence).unwrap_unchecked();
    auto bytes = Vec<u8>::make();
    for (;;) {
        auto next = input.template next<u8>();
        if (next.is_err()) return Err(rstd::move(next).unwrap_err_unchecked());
        auto value = rstd::move(next).unwrap_unchecked();
        if (value.is_none()) break;
        bytes.push(rstd::move(value).unwrap_unchecked());
    }
    auto ended = input.end();
    if (ended.is_err()) return Err(rstd::move(ended).unwrap_err_unchecked());
    return Ok(rstd::move(bytes));
}

static_assert(serde::Deserializer<DirectDeserializer>);

export template<typename T>
auto decode_direct(ref<str> input, ParseOptions options = {}) -> Result<T, serde::Error> {
    auto reader       = JsonReader(input, options);
    auto deserializer = DirectDeserializer(reader);
    auto value        = serde::deserialize<T>(deserializer);
    if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
    auto finished = reader.finish();
    if (finished.is_err()) {
        return Err(
            direct_syntax_error(serde::DataPath {}, rstd::move(finished).unwrap_err_unchecked()));
    }
    return value;
}

} // namespace rstd::json

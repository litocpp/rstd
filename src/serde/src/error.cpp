module rstd.serde;

using namespace rstd::literals;

namespace rstd::serde
{

PathSegment::PathSegment(PathSegmentKind kind, Option<String> name, usize index)
    : kind_(kind), name_(rstd::move(name)), index_(index) {
}

auto PathSegment::field(ref<str> name) -> PathSegment {
    return PathSegment(PathSegmentKind::Field, Some(String::make(name)), usize());
}

auto PathSegment::index(usize value) -> PathSegment {
    return PathSegment(PathSegmentKind::Index, None(), value);
}

auto PathSegment::map_key(ref<str> name) -> PathSegment {
    return PathSegment(PathSegmentKind::MapKey, Some(String::make(name)), usize());
}

auto PathSegment::variant(ref<str> name) -> PathSegment {
    return PathSegment(PathSegmentKind::Variant, Some(String::make(name)), usize());
}

auto PathSegment::clone() const -> PathSegment {
    return PathSegment(
        kind_, name_.is_some() ? Some(name_->clone()) : Option<String>(None()), index_);
}

DataPath::DataPath(): segments_(Vec<PathSegment>::make()) {
}

DataPath::DataPath(Vec<PathSegment> segments): segments_(rstd::move(segments)) {
}

auto DataPath::clone() const -> DataPath {
    auto segments = Vec<PathSegment>::with_capacity(segments_.len());
    for (const auto& segment : segments_) segments.push(segment.clone());
    return DataPath(rstd::move(segments));
}

auto DataPath::with_field(ref<str> name) const -> DataPath {
    auto path = clone();
    path.segments_.push(PathSegment::field(name));
    return path;
}

auto DataPath::with_index(usize index) const -> DataPath {
    auto path = clone();
    path.segments_.push(PathSegment::index(index));
    return path;
}

auto DataPath::with_map_key(ref<str> name) const -> DataPath {
    auto path = clone();
    path.segments_.push(PathSegment::map_key(name));
    return path;
}

auto DataPath::with_variant(ref<str> name) const -> DataPath {
    auto path = clone();
    path.segments_.push(PathSegment::variant(name));
    return path;
}

Error::Error(ErrorKind                      kind,
             DataPath                       path,
             Option<ValueKind>              expected,
             Option<ValueKind>              actual,
             Option<String>                 message,
             Option<Box<dyn<error::Error>>> source)
    : kind_(kind),
      path_(rstd::move(path)),
      expected_(rstd::move(expected)),
      actual_(rstd::move(actual)),
      message_(rstd::move(message)),
      source_(rstd::move(source)) {
}

auto Error::type_mismatch(DataPath path, ValueKind expected, ValueKind actual) -> Error {
    return Error(
        ErrorKind::TypeMismatch, rstd::move(path), Some(expected), Some(actual), None(), None());
}

auto Error::missing_field(DataPath path, ref<str> field) -> Error {
    return Error(ErrorKind::MissingField,
                 path.with_field(field),
                 None(),
                 None(),
                 Some(String::make(field)),
                 None());
}

auto Error::unknown_field(DataPath path, ref<str> field) -> Error {
    return Error(ErrorKind::UnknownField,
                 path.with_field(field),
                 None(),
                 None(),
                 Some(String::make(field)),
                 None());
}

auto Error::unknown_variant(DataPath path, ref<str> variant) -> Error {
    return Error(ErrorKind::UnknownVariant,
                 path.with_variant(variant),
                 None(),
                 None(),
                 Some(String::make(variant)),
                 None());
}

auto Error::duplicate_field(DataPath path, ref<str> field) -> Error {
    return Error(ErrorKind::DuplicateField,
                 path.with_field(field),
                 None(),
                 None(),
                 Some(String::make(field)),
                 None());
}

auto Error::invalid_value(DataPath path, ref<str> message) -> Error {
    return Error(ErrorKind::InvalidValue,
                 rstd::move(path),
                 None(),
                 None(),
                 Some(String::make(message)),
                 None());
}

auto Error::invariant(DataPath path, ref<str> message) -> Error {
    return Error(ErrorKind::Invariant,
                 rstd::move(path),
                 None(),
                 None(),
                 Some(String::make(message)),
                 None());
}

auto Error::unsupported(DataPath path, ref<str> message) -> Error {
    return Error(ErrorKind::Unsupported,
                 rstd::move(path),
                 None(),
                 None(),
                 Some(String::make(message)),
                 None());
}

auto Error::unexpected_end(DataPath path) -> Error {
    return Error(ErrorKind::UnexpectedEnd, rstd::move(path), None(), None(), None(), None());
}

auto value_kind_name(ValueKind kind) noexcept -> ref<str> {
    switch (kind) {
    case ValueKind::Null: return "null"_str;
    case ValueKind::Unit: return "unit"_str;
    case ValueKind::Boolean: return "boolean"_str;
    case ValueKind::SignedInteger: return "signed integer"_str;
    case ValueKind::UnsignedInteger: return "unsigned integer"_str;
    case ValueKind::Float: return "float"_str;
    case ValueKind::String: return "string"_str;
    case ValueKind::Bytes: return "bytes"_str;
    case ValueKind::Sequence: return "sequence"_str;
    case ValueKind::Map: return "map"_str;
    case ValueKind::Enum: return "enum"_str;
    case ValueKind::Extension: return "extension"_str;
    }
    return "value"_str;
}

} // namespace rstd::serde

namespace rstd
{

auto Impl<fmt::Display, serde::Error>::fmt(fmt::Formatter& formatter) const -> bool {
    const auto& error = this->self();
    if (error.kind() == serde::ErrorKind::TypeMismatch) {
        if (! formatter.write_fmt(fmt::Arguments::make("type mismatch: expected {}, found {}",
                                                       serde::value_kind_name(*error.expected()),
                                                       serde::value_kind_name(*error.actual())))) {
            return false;
        }
    } else {
        ref<str> text = "serde error"_str;
        switch (error.kind()) {
        case serde::ErrorKind::TypeMismatch: break;
        case serde::ErrorKind::MissingField: text = "missing field"_str; break;
        case serde::ErrorKind::UnknownField: text = "unknown field"_str; break;
        case serde::ErrorKind::UnknownVariant: text = "unknown variant"_str; break;
        case serde::ErrorKind::DuplicateField: text = "duplicate field"_str; break;
        case serde::ErrorKind::InvalidValue: text = "invalid value"_str; break;
        case serde::ErrorKind::Invariant: text = "serde protocol invariant failed"_str; break;
        case serde::ErrorKind::Unsupported: text = "unsupported value"_str; break;
        case serde::ErrorKind::UnexpectedEnd: text = "unexpected end of input"_str; break;
        }
        if (! formatter.write_str(text)) return false;
        auto message = error.message();
        if (message.is_some() && ! formatter.write_fmt(fmt::Arguments::make(": {}", *message))) {
            return false;
        }
    }

    if (! formatter.write_raw(" at $", sizeof(" at $") - 1)) return false;
    for (const auto& segment : error.path().segments()) {
        switch (segment.kind()) {
        case serde::PathSegmentKind::Field:
        case serde::PathSegmentKind::MapKey:
            if (! formatter.write_fmt(fmt::Arguments::make(".{}", *segment.name()))) return false;
            break;
        case serde::PathSegmentKind::Index:
            if (! formatter.write_fmt(fmt::Arguments::make("[{}]", segment.index()))) return false;
            break;
        case serde::PathSegmentKind::Variant:
            if (! formatter.write_fmt(fmt::Arguments::make("::{}", *segment.name()))) return false;
            break;
        }
    }
    return true;
}

auto Impl<fmt::Debug, serde::Error>::fmt(fmt::Formatter& formatter) const -> bool {
    return as<fmt::Display>(this->self()).fmt(formatter);
}

} // namespace rstd

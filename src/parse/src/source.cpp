module rstd.parse;

namespace rstd::parse
{

SourceId::SourceId() = default;

SourceId::SourceId(ref<str> value): value_(String::make(value)) {
}

SourceId::SourceId(String value): value_(rstd::move(value)) {
}

auto SourceId::as_str() const noexcept -> ref<str> {
    return value_.as_str();
}

auto SourceId::clone() const -> SourceId {
    return SourceId(value_.clone());
}

} // namespace rstd::parse

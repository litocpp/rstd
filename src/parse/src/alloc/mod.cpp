module rstd.parse.alloc;

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

ParseError::ParseError(Diagnostic diagnostic): diagnostic_(rstd::move(diagnostic)) {
}

auto ParseError::expected(SourceId source, Span span, RuleId rule) -> ParseError {
    return ParseError(
        Diagnostic(rstd::move(source), span, ErrorKind::Expected, Some(String::make(rule.name()))));
}

auto ParseError::stalled(SourceId source, Span span, RuleId rule) -> ParseError {
    return ParseError(
        Diagnostic(rstd::move(source), span, ErrorKind::Stalled, Some(String::make(rule.name()))));
}

auto ParseError::capacity(SourceId source, Span span, RuleId rule) -> ParseError {
    return ParseError(
        Diagnostic(rstd::move(source), span, ErrorKind::Capacity, Some(String::make(rule.name()))));
}

} // namespace rstd::parse

namespace rstd
{

auto Impl<fmt::Display, parse::ParseError>::fmt(fmt::Formatter& formatter) const -> bool {
    if (this->self().diagnostic().kind() == parse::ErrorKind::Capacity) {
        if (! formatter.write_raw("parse collection capacity exceeded for ",
                                  sizeof("parse collection capacity exceeded for ") - 1)) {
            return false;
        }
    } else if (this->self().diagnostic().kind() == parse::ErrorKind::Stalled) {
        if (! formatter.write_raw("parse rule made no progress: ",
                                  sizeof("parse rule made no progress: ") - 1)) {
            return false;
        }
    } else {
        if (! formatter.write_raw("expected ", sizeof("expected ") - 1)) return false;
    }

    auto expected = this->self().diagnostic().expected();
    if (expected.is_none()) return true;
    return formatter.write_raw(expected->data(), expected->size().to_primitive());
}

auto Impl<fmt::Debug, parse::ParseError>::fmt(fmt::Formatter& formatter) const -> bool {
    return as<fmt::Display>(this->self()).fmt(formatter);
}

} // namespace rstd

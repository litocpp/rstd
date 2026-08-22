module rstd.parse;

namespace rstd::parse
{

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

} // namespace rstd::parse

namespace rstd
{

auto Impl<fmt::Display, parse::ParseError>::fmt(fmt::Formatter& formatter) const -> bool {
    auto expected = this->self().diagnostic().expected();
    if (expected.is_some()) {
        if (! formatter.write_raw("expected ", sizeof("expected ") - 1)) return false;
        return formatter.write_raw(expected->data(), expected->size().to_primitive());
    }
    return formatter.write_raw("parse error", sizeof("parse error") - 1);
}

auto Impl<fmt::Debug, parse::ParseError>::fmt(fmt::Formatter& formatter) const -> bool {
    return as<fmt::Display>(this->self()).fmt(formatter);
}

} // namespace rstd

export module rstd.parse:error;
export import :source;

using namespace rstd::prelude;
using ::alloc::string::String;

export namespace rstd::parse
{

enum class ErrorKind : rstd::uint8_t
{
    Expected,
    Stalled,
};

class Diagnostic {
    SourceId       source_;
    Span           span_;
    ErrorKind      kind_;
    Option<String> expected_;

public:
    Diagnostic(SourceId source, Span span, ErrorKind kind, Option<String> expected)
        : source_(rstd::move(source)), span_(span), kind_(kind), expected_(rstd::move(expected)) {}

    auto source() const noexcept [[clang::lifetimebound]] -> const SourceId& { return source_; }
    constexpr auto span() const noexcept -> Span { return span_; }
    constexpr auto kind() const noexcept -> ErrorKind { return kind_; }
    auto           expected() const noexcept [[clang::lifetimebound]] -> Option<ref<str>> {
        if (expected_.is_none()) return None();
        return Some(expected_->as_str());
    }
};

class ParseError {
    Diagnostic diagnostic_;

public:
    explicit ParseError(Diagnostic diagnostic);

    static auto expected(SourceId source, Span span, RuleId rule) -> ParseError;

    static auto stalled(SourceId source, Span span, RuleId rule) -> ParseError;

    auto diagnostic() const noexcept [[clang::lifetimebound]] -> const Diagnostic& {
        return diagnostic_;
    }
};

template<typename T>
using Match = Result<Option<T>, ParseError>;

} // namespace rstd::parse

namespace rstd
{

template<>
struct Impl<fmt::Display, parse::ParseError> : ImplBase<parse::ParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, parse::ParseError> : ImplBase<parse::ParseError> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, parse::ParseError> : DefaultInImpl<error::Error, parse::ParseError> {};

} // namespace rstd

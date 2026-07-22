export module rstd.toml:error;
export import rstd.core;
import rstd.alloc;

using namespace rstd::prelude;

enum class TomlErrorCode : rstd::uint8_t
{
    UnexpectedEnd,
    UnexpectedCharacter,
    ExpectedKey,
    ExpectedEquals,
    ExpectedValue,
    ExpectedCommaOrEnd,
    ExpectedLineEnd,
    InvalidUtf8,
    InvalidEscape,
    InvalidUnicode,
    InvalidString,
    InvalidNumber,
    NumberOutOfRange,
    InvalidDateTime,
    DuplicateKey,
    DuplicateTable,
    TableRedefinition,
    RecursionLimitExceeded,
    ResourceLimitExceeded,
};

class TomlParser;

export namespace rstd::toml
{

enum class ErrorCategory : rstd::uint8_t
{
    Syntax,
    Eof,
    Limit,
};

class Error {
    TomlErrorCode code_;
    usize         line_;
    usize         column_;
    usize         offset_;

    constexpr Error(TomlErrorCode code, usize line, usize column, usize offset) noexcept
        : code_(code), line_(line), column_(column), offset_(offset) {}

    friend class ::TomlParser;
    template<typename, typename>
    friend struct rstd::Impl;

public:
    [[nodiscard]]
    constexpr auto line() const noexcept -> usize {
        return line_;
    }

    [[nodiscard]]
    constexpr auto column() const noexcept -> usize {
        return column_;
    }

    [[nodiscard]]
    constexpr auto offset() const noexcept -> usize {
        return offset_;
    }

    [[nodiscard]]
    constexpr auto classify() const noexcept -> ErrorCategory {
        if (code_ == TomlErrorCode::UnexpectedEnd) return ErrorCategory::Eof;
        if (code_ == TomlErrorCode::RecursionLimitExceeded ||
            code_ == TomlErrorCode::ResourceLimitExceeded) {
            return ErrorCategory::Limit;
        }
        return ErrorCategory::Syntax;
    }

    [[nodiscard]]
    constexpr auto is_syntax() const noexcept -> bool {
        return classify() == ErrorCategory::Syntax;
    }

    [[nodiscard]]
    constexpr auto is_eof() const noexcept -> bool {
        return classify() == ErrorCategory::Eof;
    }

    [[nodiscard]]
    constexpr auto is_limit() const noexcept -> bool {
        return classify() == ErrorCategory::Limit;
    }
};

} // namespace rstd::toml

namespace rstd
{

template<>
struct Impl<fmt::Display, toml::Error> : ImplBase<toml::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, toml::Error> : ImplBase<toml::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

auto Impl<fmt::Display, toml::Error>::fmt(fmt::Formatter& formatter) const -> bool {
    const char* message = "TOML syntax error";
    switch (this->self().code_) {
    case TomlErrorCode::UnexpectedEnd: message = "unexpected end of TOML input"; break;
    case TomlErrorCode::UnexpectedCharacter: message = "unexpected character"; break;
    case TomlErrorCode::ExpectedKey: message = "expected a key"; break;
    case TomlErrorCode::ExpectedEquals: message = "expected `=`"; break;
    case TomlErrorCode::ExpectedValue: message = "expected a value"; break;
    case TomlErrorCode::ExpectedCommaOrEnd: message = "expected `,` or container end"; break;
    case TomlErrorCode::ExpectedLineEnd: message = "expected newline or end of input"; break;
    case TomlErrorCode::InvalidUtf8: message = "TOML input is not valid UTF-8"; break;
    case TomlErrorCode::InvalidEscape: message = "invalid string escape"; break;
    case TomlErrorCode::InvalidUnicode: message = "invalid Unicode scalar value"; break;
    case TomlErrorCode::InvalidString: message = "invalid string"; break;
    case TomlErrorCode::InvalidNumber: message = "invalid number"; break;
    case TomlErrorCode::NumberOutOfRange: message = "number out of range"; break;
    case TomlErrorCode::InvalidDateTime: message = "invalid date or time"; break;
    case TomlErrorCode::DuplicateKey: message = "duplicate key"; break;
    case TomlErrorCode::DuplicateTable: message = "duplicate table"; break;
    case TomlErrorCode::TableRedefinition: message = "table cannot be redefined"; break;
    case TomlErrorCode::RecursionLimitExceeded: message = "TOML recursion limit exceeded"; break;
    case TomlErrorCode::ResourceLimitExceeded: message = "TOML resource limit exceeded"; break;
    }
    return formatter.write_fmt(fmt::Arguments::make("{} at line {} column {} byte {}",
                                                    message,
                                                    this->self().line_,
                                                    this->self().column_,
                                                    this->self().offset_));
}

} // namespace rstd

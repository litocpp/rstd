module;
#include <rstd/macro.hpp>

export module rstd.json:error;
export import rstd.core;
import rstd.alloc;

using namespace rstd::prelude;

enum class ErrorCode : u8
{
    EofWhileParsingList,
    EofWhileParsingObject,
    EofWhileParsingComment,
    EofWhileParsingString,
    EofWhileParsingValue,
    ExpectedColon,
    ExpectedListCommaOrEnd,
    ExpectedObjectCommaOrEnd,
    ExpectedSomeIdent,
    ExpectedSomeValue,
    InvalidEscape,
    InvalidNumber,
    NumberOutOfRange,
    InvalidUnicodeCodePoint,
    ControlCharacterWhileParsingString,
    KeyMustBeAString,
    LoneLeadingSurrogateInHexEscape,
    TrailingComma,
    TrailingCharacters,
    UnexpectedEndOfHexEscape,
    RecursionLimitExceeded,
};

class Parser;

export namespace rstd::json
{

enum class Category : u8
{
    Syntax,
    Eof,
};

class Error {
    ErrorCode code_;
    usize     line_;
    usize     column_;

    constexpr Error(ErrorCode code, usize line, usize column) noexcept
        : code_(code), line_(line), column_(column) {}

    friend class ::Parser;
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
    constexpr auto classify() const noexcept -> Category {
        switch (code_) {
        case ErrorCode::EofWhileParsingList:
        case ErrorCode::EofWhileParsingObject:
        case ErrorCode::EofWhileParsingComment:
        case ErrorCode::EofWhileParsingString:
        case ErrorCode::EofWhileParsingValue: return Category::Eof;
        default: return Category::Syntax;
        }
    }

    [[nodiscard]]
    constexpr auto is_syntax() const noexcept -> bool {
        return classify() == Category::Syntax;
    }
    [[nodiscard]]
    constexpr auto is_eof() const noexcept -> bool {
        return classify() == Category::Eof;
    }
};

} // namespace rstd::json

namespace rstd
{

template<>
struct Impl<fmt::Display, json::Error> : ImplBase<json::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, json::Error> : ImplBase<json::Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return as<fmt::Display>(this->self()).fmt(formatter);
    }
};

} // namespace rstd

namespace rstd
{

auto Impl<fmt::Display, json::Error>::fmt(fmt::Formatter& formatter) const -> bool {
    const char* message = "JSON syntax error";
    switch (this->self().code_) {
    case ErrorCode::EofWhileParsingList: message = "EOF while parsing a list"; break;
    case ErrorCode::EofWhileParsingObject: message = "EOF while parsing an object"; break;
    case ErrorCode::EofWhileParsingComment: message = "EOF while parsing a comment"; break;
    case ErrorCode::EofWhileParsingString: message = "EOF while parsing a string"; break;
    case ErrorCode::EofWhileParsingValue: message = "EOF while parsing a value"; break;
    case ErrorCode::ExpectedColon: message = "expected `:`"; break;
    case ErrorCode::ExpectedListCommaOrEnd: message = "expected `,` or `]`"; break;
    case ErrorCode::ExpectedObjectCommaOrEnd: message = "expected `,` or `}`"; break;
    case ErrorCode::ExpectedSomeIdent: message = "expected ident"; break;
    case ErrorCode::ExpectedSomeValue: message = "expected value"; break;
    case ErrorCode::InvalidEscape: message = "invalid escape"; break;
    case ErrorCode::InvalidNumber: message = "invalid number"; break;
    case ErrorCode::NumberOutOfRange: message = "number out of range"; break;
    case ErrorCode::InvalidUnicodeCodePoint: message = "invalid unicode code point"; break;
    case ErrorCode::ControlCharacterWhileParsingString:
        message = "control character (\\u0000-\\u001F) found while parsing a string";
        break;
    case ErrorCode::KeyMustBeAString: message = "key must be a string"; break;
    case ErrorCode::LoneLeadingSurrogateInHexEscape:
        message = "lone leading surrogate in hex escape";
        break;
    case ErrorCode::TrailingComma: message = "trailing comma"; break;
    case ErrorCode::TrailingCharacters: message = "trailing characters"; break;
    case ErrorCode::UnexpectedEndOfHexEscape: message = "unexpected end of hex escape"; break;
    case ErrorCode::RecursionLimitExceeded: message = "recursion limit exceeded"; break;
    }
    return formatter.write_fmt(fmt::Arguments::make(
        "{} at line {} column {}", message, this->self().line_, this->self().column_));
}

} // namespace rstd

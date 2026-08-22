export module rstd.json:parser;
export import :value;
export import :error;
import rstd.parse;

export namespace rstd::json
{

/// The result of parsing a JSON value.
using ParseResult = rstd::Result<Value, Error>;

/// Options that control accepted JSON syntax.
struct ParseOptions {
    /// Allows line and block comments when enabled.
    bool allow_comments { false };
    /// Rejects repeated keys in the same object when enabled.
    bool reject_duplicate_keys { false };
};

/// Parses one JSON value from UTF-8 text.
auto from_str(ref<str> input) -> ParseResult;
/// Parses one JSON value from UTF-8 text using explicit options.
auto from_str(ref<str> input, ParseOptions options) -> ParseResult;
/// Validates UTF-8 bytes and parses one JSON value.
auto from_slice(slice<u8> input) -> ParseResult;
/// Validates UTF-8 bytes and parses one JSON value using explicit options.
auto from_slice(slice<u8> input, ParseOptions options) -> ParseResult;

} // namespace rstd::json

using namespace rstd::prelude;
using namespace rstd::json;
using namespace rstd::literals;

class Parser {
    rstd::parse::TextCursor cursor_;
    u8                      remaining_depth_ { 128 };
    ParseOptions            options_;

    [[nodiscard]]
    auto eof() const noexcept -> bool {
        return cursor_.is_eof();
    }

    [[nodiscard]]
    auto peek() const noexcept -> u8 {
        auto value = cursor_.peek();
        return value.is_none() ? u8() : value->get();
    }

    [[nodiscard]]
    auto peek_next() const noexcept -> u8 {
        auto value = cursor_.peek(usize(1));
        return value.is_none() ? u8() : value->get();
    }

    auto take() noexcept -> u8 { return cursor_.take()->get(); }

    auto consume_whitespace() noexcept -> Option<Error> {
        while (! eof()) {
            switch (peek().to_primitive()) {
            case ' ':
            case '\n':
            case '\r':
            case '\t': take(); break;
            default:
                if (! options_.allow_comments || peek() != u8('/')) return None();
                if (peek_next() == u8('/')) {
                    take();
                    take();
                    while (! eof() && peek() != u8('\n')) take();
                    break;
                }
                if (peek_next() == u8('*')) {
                    take();
                    take();
                    bool closed = false;
                    while (! eof()) {
                        if (peek() == u8('*') && peek_next() == u8('/')) {
                            take();
                            take();
                            closed = true;
                            break;
                        }
                        take();
                    }
                    if (! closed) {
                        return Some(error(ErrorCode::EofWhileParsingComment));
                    }
                    break;
                }
                return None();
            }
        }
        return None();
    }

    [[nodiscard]]
    auto error(ErrorCode code) const noexcept -> Error {
        const auto location = cursor_.source_position();
        return Error(code, location.line, eof() ? location.column - usize(1) : location.column);
    }

    [[nodiscard]]
    auto error_after_consumed(ErrorCode code) const noexcept -> Error {
        const auto location = cursor_.source_position();
        return Error(code, location.line, location.column - usize(1));
    }

    [[nodiscard]]
    auto parse_ident(ref<str> suffix, Value value) -> ParseResult {
        take();
        for (usize i {}; i < suffix.size(); ++i) {
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
            if (peek() != suffix[i]) return Err(error(ErrorCode::ExpectedSomeIdent));
            take();
        }
        return Ok(rstd::move(value));
    }

    [[nodiscard]]
    static auto hex_value(u8 byte) noexcept -> Option<u8> {
        if (byte >= u8('0') && byte <= u8('9')) return Some(byte - u8('0'));
        if (byte >= u8('a') && byte <= u8('f')) return Some(byte - u8('a') + u8(10));
        if (byte >= u8('A') && byte <= u8('F')) return Some(byte - u8('A') + u8(10));
        return None();
    }

    [[nodiscard]]
    auto parse_hex_escape() -> Result<u16, Error> {
        u16 value {};
        for (usize i {}; i < usize(4); ++i) {
            if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
            auto digit = hex_value(peek());
            if (digit.is_none()) return Err(error(ErrorCode::InvalidEscape));
            take();
            value = (value << u64(4)) | rstd::as_cast<u16>(*digit);
        }
        return Ok(rstd::move(value));
    }

    [[nodiscard]]
    auto parse_unicode_escape(::alloc::string::String& output) -> Result<empty, Error> {
        auto first_result = parse_hex_escape();
        if (first_result.is_err()) return Err(first_result.unwrap_err());
        const u16 first = first_result.unwrap();

        if (first >= u16(0xdc00) && first <= u16(0xdfff)) {
            return Err(error_after_consumed(ErrorCode::LoneLeadingSurrogateInHexEscape));
        }
        if (first < u16(0xd800) || first > u16(0xdbff)) {
            output.push(static_cast<char32_t>(first.to_primitive()));
            return Ok(empty {});
        }

        if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
        if (peek() != u8('\\')) {
            return Err(error(ErrorCode::UnexpectedEndOfHexEscape));
        }
        take();
        if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
        if (peek() != u8('u')) {
            return Err(error(ErrorCode::UnexpectedEndOfHexEscape));
        }
        take();

        auto second_result = parse_hex_escape();
        if (second_result.is_err()) return Err(second_result.unwrap_err());
        const u16 second = second_result.unwrap();
        if (second < u16(0xdc00) || second > u16(0xdfff)) {
            return Err(error_after_consumed(ErrorCode::LoneLeadingSurrogateInHexEscape));
        }

        const u32 scalar = ((((rstd::as_cast<u32>(first) - u32(0xd800)) << u64(10)) |
                             (rstd::as_cast<u32>(second) - u32(0xdc00))) +
                            u32(0x10000));
        output.push(static_cast<char32_t>(scalar.to_primitive()));
        return Ok(empty {});
    }

    [[nodiscard]]
    auto parse_string() -> Result<::alloc::string::String, Error> {
        take();
        auto output = ::alloc::string::String::make();

        while (! eof()) {
            auto chunk_start = cursor_.checkpoint();
            while (! eof() && peek() != u8('"') && peek() != u8('\\') && peek() >= u8(0x20)) {
                take();
            }
            output.push_str(cursor_.consumed_text(chunk_start));
            if (eof()) break;

            const u8 byte = peek();
            if (byte == u8('"')) {
                take();
                return Ok(rstd::move(output));
            }
            if (byte < u8(0x20)) {
                take();
                return Err(error_after_consumed(ErrorCode::ControlCharacterWhileParsingString));
            }
            take();
            if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
            switch (take().to_primitive()) {
            case '"': output.push(U'"'); break;
            case '\\': output.push(U'\\'); break;
            case '/': output.push(U'/'); break;
            case 'b': output.push(U'\b'); break;
            case 'f': output.push(U'\f'); break;
            case 'n': output.push(U'\n'); break;
            case 'r': output.push(U'\r'); break;
            case 't': output.push(U'\t'); break;
            case 'u': {
                auto decoded = parse_unicode_escape(output);
                if (decoded.is_err()) return Err(decoded.unwrap_err());
                break;
            }
            default: return Err(error_after_consumed(ErrorCode::InvalidEscape));
            }
        }

        return Err(error(ErrorCode::EofWhileParsingString));
    }

    [[nodiscard]]
    auto
    parse_float(rstd::parse::Span integer, rstd::parse::Span fraction, i32 exponent, bool negative)
        -> ParseResult {
        auto parsed = rstd::num::dec2flt::to_f64({
            .integer  = cursor_.view(integer),
            .fraction = cursor_.view(fraction),
            .exponent = exponent,
            .negative = negative,
        });
        if (parsed.is_err()) {
            const auto cause = parsed.unwrap_err();
            return Err(error(cause == rstd::num::dec2flt::Error::Overflow
                                 ? ErrorCode::NumberOutOfRange
                                 : ErrorCode::InvalidNumber));
        }

        auto number = Number::from_f64(parsed.unwrap());
        if (number.is_none()) return Err(error(ErrorCode::NumberOutOfRange));
        return Ok(Value::Number(*number));
    }

    [[nodiscard]]
    auto parse_number() -> ParseResult {
        bool negative = false;
        if (peek() == u8('-')) {
            negative = true;
            take();
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
        }

        auto integer_begin = cursor_.checkpoint();
        if (peek() == u8('0')) {
            take();
            if (! eof() && peek() >= u8('0') && peek() <= u8('9')) {
                return Err(error(ErrorCode::InvalidNumber));
            }
        } else if (peek() >= u8('1') && peek() <= u8('9')) {
            do {
                take();
            } while (! eof() && peek() >= u8('0') && peek() <= u8('9'));
        } else {
            return Err(error(ErrorCode::InvalidNumber));
        }
        const auto integer = cursor_.span_from(integer_begin);

        bool              floating = false;
        rstd::parse::Span fraction { .begin = integer.end, .end = integer.end };
        if (! eof() && peek() == u8('.')) {
            floating = true;
            take();
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
            if (peek() < u8('0') || peek() > u8('9')) {
                return Err(error(ErrorCode::InvalidNumber));
            }
            auto fraction_begin = cursor_.checkpoint();
            do {
                take();
            } while (! eof() && peek() >= u8('0') && peek() <= u8('9'));
            fraction = cursor_.span_from(fraction_begin);
        }

        i32 exponent {};
        if (! eof() && (peek() == u8('e') || peek() == u8('E'))) {
            floating = true;
            take();
            bool exponent_negative = false;
            if (! eof() && (peek() == u8('+') || peek() == u8('-'))) {
                exponent_negative = peek() == u8('-');
                take();
            }
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
            if (peek() < u8('0') || peek() > u8('9')) {
                return Err(error(ErrorCode::InvalidNumber));
            }
            do {
                if (exponent < i32(10'000)) {
                    exponent = exponent * i32(10) + rstd::as_cast<i32>(peek() - u8('0'));
                    if (exponent > i32(10'000)) exponent = i32(10'000);
                }
                take();
            } while (! eof() && peek() >= u8('0') && peek() <= u8('9'));
            if (exponent_negative) exponent = -exponent;
        }

        if (floating) {
            return parse_float(integer, fraction, exponent, negative);
        }

        auto  digits_view = cursor_.view(integer);
        usize digits {};
        u64   magnitude {};
        bool  overflow = false;
        for (; digits < digits_view.len(); ++digits) {
            const u64 digit = rstd::as_cast<u64>(digits_view[digits] - u8('0'));
            if (magnitude > (u64::MAX - digit) / u64(10)) {
                overflow = true;
                break;
            }
            magnitude = magnitude * u64(10) + digit;
        }
        if (overflow) {
            return parse_float(integer,
                               rstd::parse::Span { .begin = integer.end, .end = integer.end },
                               i32(),
                               negative);
        }

        if (! negative) return Ok(Value::Number(Number::from_u64(magnitude)));
        if (magnitude == u64()) {
            return parse_float(integer,
                               rstd::parse::Span { .begin = integer.end, .end = integer.end },
                               i32(),
                               negative);
        }
        const u64 min_magnitude = rstd::as_cast<u64>(i64::MAX) + u64(1);
        if (magnitude > min_magnitude) {
            return parse_float(integer,
                               rstd::parse::Span { .begin = integer.end, .end = integer.end },
                               i32(),
                               negative);
        }
        const i64 signed_value =
            magnitude == min_magnitude ? i64::MIN : -rstd::as_cast<i64>(magnitude);
        return Ok(Value::Number(Number::from_i64(signed_value)));
    }

    [[nodiscard]]
    auto parse_array() -> ParseResult {
        if (remaining_depth_ == u8(1)) return Err(error(ErrorCode::RecursionLimitExceeded));
        --remaining_depth_;
        take();
        if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);

        auto values = Array::make();
        if (! eof() && peek() == u8(']')) {
            take();
            ++remaining_depth_;
            return Ok(Value::Array(rstd::move(values)));
        }

        for (;;) {
            if (eof()) return Err(error(ErrorCode::EofWhileParsingList));
            auto value = parse_value();
            if (value.is_err()) return Err(value.unwrap_err());
            values.push(value.unwrap());
            if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);

            if (eof()) return Err(error(ErrorCode::EofWhileParsingList));
            if (peek() == u8(']')) {
                take();
                ++remaining_depth_;
                return Ok(Value::Array(rstd::move(values)));
            }
            if (peek() != u8(',')) return Err(error(ErrorCode::ExpectedListCommaOrEnd));
            take();
            if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
            if (peek() == u8(']')) return Err(error(ErrorCode::TrailingComma));
        }
    }

    [[nodiscard]]
    auto parse_object() -> ParseResult {
        if (remaining_depth_ == u8(1)) return Err(error(ErrorCode::RecursionLimitExceeded));
        --remaining_depth_;
        take();
        if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);

        auto values = Map::make();
        if (! eof() && peek() == u8('}')) {
            take();
            ++remaining_depth_;
            return Ok(Value::Object(rstd::move(values)));
        }

        for (;;) {
            if (eof()) return Err(error(ErrorCode::EofWhileParsingObject));
            if (peek() != u8('"')) return Err(error(ErrorCode::KeyMustBeAString));
            auto key = parse_string();
            if (key.is_err()) return Err(key.unwrap_err());
            if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);

            if (eof()) return Err(error(ErrorCode::EofWhileParsingObject));
            if (peek() != u8(':')) return Err(error(ErrorCode::ExpectedColon));
            take();
            auto value = parse_value();
            if (value.is_err()) return Err(value.unwrap_err());
            auto replaced = values.insert(key.unwrap(), value.unwrap());
            if (options_.reject_duplicate_keys && replaced.is_some()) {
                return Err(error(ErrorCode::DuplicateObjectKey));
            }
            if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);

            if (eof()) return Err(error(ErrorCode::EofWhileParsingObject));
            if (peek() == u8('}')) {
                take();
                ++remaining_depth_;
                return Ok(Value::Object(rstd::move(values)));
            }
            if (peek() != u8(',')) return Err(error(ErrorCode::ExpectedObjectCommaOrEnd));
            take();
            if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
            if (peek() == u8('}')) return Err(error(ErrorCode::TrailingComma));
            if (peek() != u8('"')) return Err(error(ErrorCode::KeyMustBeAString));
        }
    }

public:
    explicit Parser(ref<str> input, ParseOptions options = {}) noexcept
        : cursor_(rstd::parse::text_input(input)), options_(options) {}

    [[nodiscard]]
    static auto invalid_unicode_error() noexcept -> Error {
        return Error(ErrorCode::InvalidUnicodeCodePoint, usize(1), usize(1));
    }

    [[nodiscard]]
    auto parse_value() -> ParseResult {
        if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));

        switch (peek().to_primitive()) {
        case 'n': return parse_ident("ull"_str, Value::Null());
        case 't': return parse_ident("rue"_str, Value::Bool(true));
        case 'f': return parse_ident("alse"_str, Value::Bool(false));
        case '"': {
            auto value = parse_string();
            if (value.is_err()) return Err(value.unwrap_err());
            return Ok(Value::String(value.unwrap()));
        }
        case '[': return parse_array();
        case '{': return parse_object();
        case '-': return parse_number();
        default:
            if (peek() >= u8('0') && peek() <= u8('9')) return parse_number();
            return Err(error(ErrorCode::ExpectedSomeValue));
        }
    }

    [[nodiscard]]
    auto parse() -> ParseResult {
        auto value = parse_value();
        if (value.is_err()) return Err(value.unwrap_err());
        if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
        if (! eof()) return Err(error(ErrorCode::TrailingCharacters));
        return value;
    }
};

namespace rstd::json
{

auto from_str(ref<str> input) -> ParseResult {
    return Parser(input).parse();
}

auto from_str(ref<str> input, ParseOptions options) -> ParseResult {
    return Parser(input, options).parse();
}

auto from_slice(slice<u8> input) -> ParseResult {
    return from_slice(input, {});
}

auto from_slice(slice<u8> input, ParseOptions options) -> ParseResult {
    auto text = str_::from_utf8(input);
    if (text.is_err()) {
        return Err(Parser::invalid_unicode_error());
    }
    return from_str(rstd::move(text).unwrap_unchecked(), options);
}

} // namespace rstd::json

namespace rstd
{

template<>
struct Impl<str_::FromStr, json::Value> : ImplBase<json::Value> {
    using Err = json::Error;
    static auto from_str(ref<str> input) -> json::ParseResult { return json::from_str(input); }
};

} // namespace rstd

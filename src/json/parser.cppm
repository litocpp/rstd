module;
#include <charconv>
#include <cmath>
#include <system_error>

export module rstd.json:parser;
export import :value;
export import :error;

export namespace rstd::json
{

using ParseResult = rstd::Result<Value, Error>;

struct ParseOptions {
    bool allow_comments { false };
};

auto from_str(ref<str> input) -> ParseResult;
auto from_str(ref<str> input, ParseOptions options) -> ParseResult;
auto from_slice(slice<u8> input) -> ParseResult;
auto from_slice(slice<u8> input, ParseOptions options) -> ParseResult;

} // namespace rstd::json

namespace rstd::json::detail
{

class Parser {
    ref<str>     input_;
    usize        offset_ { 0 };
    usize        line_ { 1 };
    usize        column_ { 1 };
    u8           remaining_depth_ { 128 };
    ParseOptions options_;

    [[nodiscard]]
    auto eof() const noexcept -> bool {
        return offset_ == input_.size();
    }

    [[nodiscard]]
    auto peek() const noexcept -> u8 {
        return eof() ? u8(0) : input_.data()[offset_];
    }

    [[nodiscard]]
    auto peek_next() const noexcept -> u8 {
        return offset_ + 1 >= input_.size() ? u8(0) : input_.data()[offset_ + 1];
    }

    auto take() noexcept -> u8 {
        const u8 byte = input_.data()[offset_++];
        if (byte == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return byte;
    }

    auto consume_whitespace() noexcept -> Option<Error> {
        while (! eof()) {
            switch (peek()) {
            case ' ':
            case '\n':
            case '\r':
            case '\t': take(); break;
            default:
                if (! options_.allow_comments || peek() != '/') return None();
                if (peek_next() == '/') {
                    take();
                    take();
                    while (! eof() && peek() != '\n') take();
                    break;
                }
                if (peek_next() == '*') {
                    take();
                    take();
                    while (! eof()) {
                        if (peek() == '*' && peek_next() == '/') {
                            take();
                            take();
                            break;
                        }
                        take();
                    }
                    if (eof() && (offset_ < 2 || input_.data()[offset_ - 2] != '*' ||
                                  input_.data()[offset_ - 1] != '/')) {
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
        if (eof()) {
            return Error(code, line_, column_ - 1);
        }
        return Error(code, line_, column_);
    }

    [[nodiscard]]
    auto error_after_consumed(ErrorCode code) const noexcept -> Error {
        return Error(code, line_, column_ - 1);
    }

    [[nodiscard]]
    auto parse_ident(ref<str> suffix, Value value) -> ParseResult {
        take();
        for (usize i = 0; i < suffix.size(); ++i) {
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
            if (peek() != suffix.data()[i]) return Err(error(ErrorCode::ExpectedSomeIdent));
            take();
        }
        return Ok(rstd::move(value));
    }

    [[nodiscard]]
    static auto hex_value(u8 byte) noexcept -> Option<u8> {
        if (byte >= '0' && byte <= '9') return Some(u8(byte - '0'));
        if (byte >= 'a' && byte <= 'f') return Some(u8(byte - 'a' + 10));
        if (byte >= 'A' && byte <= 'F') return Some(u8(byte - 'A' + 10));
        return None();
    }

    [[nodiscard]]
    auto parse_hex_escape() -> Result<u16, Error> {
        u16 value = 0;
        for (usize i = 0; i < 4; ++i) {
            if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
            auto digit = hex_value(peek());
            if (digit.is_none()) return Err(error(ErrorCode::InvalidEscape));
            take();
            value = static_cast<u16>((value << 4) | *digit);
        }
        return Ok(rstd::move(value));
    }

    [[nodiscard]]
    auto parse_unicode_escape(::alloc::string::String& output) -> Result<empty, Error> {
        auto first_result = parse_hex_escape();
        if (first_result.is_err()) return Err(first_result.unwrap_err());
        const u16 first = first_result.unwrap();

        if (first >= 0xdc00 && first <= 0xdfff) {
            return Err(error_after_consumed(ErrorCode::LoneLeadingSurrogateInHexEscape));
        }
        if (first < 0xd800 || first > 0xdbff) {
            output.push(static_cast<char32_t>(first));
            return Ok(empty {});
        }

        if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
        if (peek() != '\\') {
            return Err(error(ErrorCode::UnexpectedEndOfHexEscape));
        }
        take();
        if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
        if (peek() != 'u') {
            return Err(error(ErrorCode::UnexpectedEndOfHexEscape));
        }
        take();

        auto second_result = parse_hex_escape();
        if (second_result.is_err()) return Err(second_result.unwrap_err());
        const u16 second = second_result.unwrap();
        if (second < 0xdc00 || second > 0xdfff) {
            return Err(error_after_consumed(ErrorCode::LoneLeadingSurrogateInHexEscape));
        }

        const u32 scalar =
            ((((static_cast<u32>(first) - 0xd800) << 10) | (static_cast<u32>(second) - 0xdc00)) +
             0x10000);
        output.push(static_cast<char32_t>(scalar));
        return Ok(empty {});
    }

    [[nodiscard]]
    auto parse_string() -> Result<::alloc::string::String, Error> {
        take();
        auto output = ::alloc::string::String::make();

        while (! eof()) {
            const usize chunk_start = offset_;
            while (! eof() && peek() != '"' && peek() != '\\' && peek() >= 0x20) take();
            if (offset_ != chunk_start) {
                output.push_str(
                    ref<str>::from_raw_parts(input_.data() + chunk_start, offset_ - chunk_start));
            }
            if (eof()) break;

            const u8 byte = peek();
            if (byte == '"') {
                take();
                return Ok(rstd::move(output));
            }
            if (byte < 0x20) {
                take();
                return Err(error_after_consumed(ErrorCode::ControlCharacterWhileParsingString));
            }
            take();
            if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
            switch (take()) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back(u8(0x08)); break;
            case 'f': output.push_back(u8(0x0c)); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
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
    auto token_is_zero(usize begin, usize end) const noexcept -> bool {
        for (usize i = begin; i < end; ++i) {
            const u8 byte = input_.data()[i];
            if (byte >= '1' && byte <= '9') return false;
        }
        return true;
    }

    [[nodiscard]]
    auto token_underflows(usize begin, usize end) const noexcept -> bool {
        usize cursor = begin;
        if (input_.data()[cursor] == '-') ++cursor;

        usize exponent_at = end;
        usize dot_at      = end;
        usize first       = end;
        for (usize i = cursor; i < end; ++i) {
            const u8 byte = input_.data()[i];
            if (byte == '.') {
                dot_at = i;
            } else if (byte == 'e' || byte == 'E') {
                exponent_at = i;
                break;
            } else if (first == end && byte >= '1' && byte <= '9') {
                first = i;
            }
        }
        if (first == end) return true;
        if (dot_at == end || dot_at > exponent_at) dot_at = exponent_at;

        i64 order = first < dot_at ? static_cast<i64>(dot_at - first - 1)
                                   : -static_cast<i64>(first - dot_at);
        if (exponent_at < end) {
            usize exp_cursor = exponent_at + 1;
            bool  negative   = false;
            if (exp_cursor < end &&
                (input_.data()[exp_cursor] == '+' || input_.data()[exp_cursor] == '-')) {
                negative = input_.data()[exp_cursor] == '-';
                ++exp_cursor;
            }
            i64 exponent = 0;
            while (exp_cursor < end && exponent < 100000) {
                exponent = exponent * 10 + (input_.data()[exp_cursor++] - '0');
            }
            order += negative ? -exponent : exponent;
        }
        return order <= -324;
    }

    [[nodiscard]]
    auto parse_float(usize begin, usize end) -> ParseResult {
        const char* first  = reinterpret_cast<const char*>(input_.data() + begin);
        const char* last   = reinterpret_cast<const char*>(input_.data() + end);
        f64         value  = 0.0;
        auto        result = std::from_chars(first, last, value, std::chars_format::general);

        if (result.ec == std::errc::result_out_of_range) {
            if (token_underflows(begin, end) || token_is_zero(begin, end)) {
                value = input_.data()[begin] == '-' ? -0.0 : 0.0;
            } else {
                return Err(error(ErrorCode::NumberOutOfRange));
            }
        } else if (result.ec != std::errc {} || result.ptr != last || ! std::isfinite(value)) {
            return Err(error(ErrorCode::InvalidNumber));
        }

        auto number = Number::from_f64(value);
        if (number.is_none()) return Err(error(ErrorCode::NumberOutOfRange));
        return Ok(Value::Number(*number));
    }

    [[nodiscard]]
    auto parse_number() -> ParseResult {
        const usize begin    = offset_;
        bool        negative = false;
        if (peek() == '-') {
            negative = true;
            take();
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
        }

        if (peek() == '0') {
            take();
            if (! eof() && peek() >= '0' && peek() <= '9') {
                return Err(error(ErrorCode::InvalidNumber));
            }
        } else if (peek() >= '1' && peek() <= '9') {
            do {
                take();
            } while (! eof() && peek() >= '0' && peek() <= '9');
        } else {
            return Err(error(ErrorCode::InvalidNumber));
        }

        bool floating = false;
        if (! eof() && peek() == '.') {
            floating = true;
            take();
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
            if (peek() < '0' || peek() > '9') return Err(error(ErrorCode::InvalidNumber));
            do {
                take();
            } while (! eof() && peek() >= '0' && peek() <= '9');
        }

        if (! eof() && (peek() == 'e' || peek() == 'E')) {
            floating = true;
            take();
            if (! eof() && (peek() == '+' || peek() == '-')) take();
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
            if (peek() < '0' || peek() > '9') return Err(error(ErrorCode::InvalidNumber));
            do {
                take();
            } while (! eof() && peek() >= '0' && peek() <= '9');
        }

        const usize end = offset_;
        if (floating) return parse_float(begin, end);

        usize digits    = begin + (negative ? 1 : 0);
        u64   magnitude = 0;
        bool  overflow  = false;
        for (; digits < end; ++digits) {
            const u64 digit = input_.data()[digits] - '0';
            if (magnitude > (u64_::MAX - digit) / 10) {
                overflow = true;
                break;
            }
            magnitude = magnitude * 10 + digit;
        }
        if (overflow) return parse_float(begin, end);

        if (! negative) return Ok(Value::Number(Number::from_u64(magnitude)));
        if (magnitude == 0) return parse_float(begin, end);
        const u64 min_magnitude = static_cast<u64>(i64_::MAX) + 1;
        if (magnitude > min_magnitude) return parse_float(begin, end);
        const i64 signed_value =
            magnitude == min_magnitude ? i64_::MIN : -static_cast<i64>(magnitude);
        return Ok(Value::Number(Number::from_i64(signed_value)));
    }

    [[nodiscard]]
    auto parse_array() -> ParseResult {
        if (remaining_depth_ == 1) return Err(error(ErrorCode::RecursionLimitExceeded));
        --remaining_depth_;
        take();
        if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);

        auto values = Array::make();
        if (! eof() && peek() == ']') {
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
            if (peek() == ']') {
                take();
                ++remaining_depth_;
                return Ok(Value::Array(rstd::move(values)));
            }
            if (peek() != ',') return Err(error(ErrorCode::ExpectedListCommaOrEnd));
            take();
            if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
            if (peek() == ']') return Err(error(ErrorCode::TrailingComma));
        }
    }

    [[nodiscard]]
    auto parse_object() -> ParseResult {
        if (remaining_depth_ == 1) return Err(error(ErrorCode::RecursionLimitExceeded));
        --remaining_depth_;
        take();
        if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);

        auto values = Map::make();
        if (! eof() && peek() == '}') {
            take();
            ++remaining_depth_;
            return Ok(Value::Object(rstd::move(values)));
        }

        for (;;) {
            if (eof()) return Err(error(ErrorCode::EofWhileParsingObject));
            if (peek() != '"') return Err(error(ErrorCode::KeyMustBeAString));
            auto key = parse_string();
            if (key.is_err()) return Err(key.unwrap_err());
            if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);

            if (eof()) return Err(error(ErrorCode::EofWhileParsingObject));
            if (peek() != ':') return Err(error(ErrorCode::ExpectedColon));
            take();
            auto value = parse_value();
            if (value.is_err()) return Err(value.unwrap_err());
            values.insert(key.unwrap(), value.unwrap());
            if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);

            if (eof()) return Err(error(ErrorCode::EofWhileParsingObject));
            if (peek() == '}') {
                take();
                ++remaining_depth_;
                return Ok(Value::Object(rstd::move(values)));
            }
            if (peek() != ',') return Err(error(ErrorCode::ExpectedObjectCommaOrEnd));
            take();
            if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
            if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
            if (peek() == '}') return Err(error(ErrorCode::TrailingComma));
            if (peek() != '"') return Err(error(ErrorCode::KeyMustBeAString));
        }
    }

public:
    explicit Parser(ref<str> input, ParseOptions options = {}) noexcept
        : input_(input), options_(options) {}

    [[nodiscard]]
    static auto invalid_unicode_error() noexcept -> Error {
        return Error(ErrorCode::InvalidUnicodeCodePoint, 1, 1);
    }

    [[nodiscard]]
    auto parse_value() -> ParseResult {
        if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));

        switch (peek()) {
        case 'n': return parse_ident("ull", Value::Null());
        case 't': return parse_ident("rue", Value::Bool(true));
        case 'f': return parse_ident("alse", Value::Bool(false));
        case '"': {
            auto value = parse_string();
            if (value.is_err()) return Err(value.unwrap_err());
            return Ok(Value::String(value.unwrap()));
        }
        case '[': return parse_array();
        case '{': return parse_object();
        case '-': return parse_number();
        default:
            if (peek() >= '0' && peek() <= '9') return parse_number();
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

} // namespace rstd::json::detail

namespace rstd::json
{

auto from_str(ref<str> input) -> ParseResult {
    return detail::Parser(input).parse();
}

auto from_str(ref<str> input, ParseOptions options) -> ParseResult {
    return detail::Parser(input, options).parse();
}

auto from_slice(slice<u8> input) -> ParseResult {
    return from_slice(input, {});
}

auto from_slice(slice<u8> input, ParseOptions options) -> ParseResult {
    auto text = str_::from_utf8(input);
    if (text.is_none()) {
        return Err(detail::Parser::invalid_unicode_error());
    }
    return from_str(*text, options);
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

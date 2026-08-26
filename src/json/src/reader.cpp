module rstd.json;
import :reader;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;

auto JsonReader::eof() const noexcept -> bool {
    return cursor_.is_eof();
}

auto JsonReader::peek() const noexcept -> u8 {
    auto value = cursor_.peek();
    return value.is_none() ? u8() : value->get();
}

auto JsonReader::peek_next() const noexcept -> u8 {
    auto value = cursor_.peek(usize(1));
    return value.is_none() ? u8() : value->get();
}

auto JsonReader::take() noexcept -> u8 {
    return cursor_.take()->get();
}

auto JsonReader::consume_whitespace() noexcept -> Option<rstd::json::Error> {
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
                if (! closed) return Some(error(ErrorCode::EofWhileParsingComment));
                break;
            }
            return None();
        }
    }
    return None();
}

auto JsonReader::error(ErrorCode code) const noexcept -> rstd::json::Error {
    const auto location = cursor_.source_position();
    return rstd::json::Error(
        code, location.line, eof() ? location.column - usize(1) : location.column);
}

auto JsonReader::error_after_consumed(ErrorCode code) const noexcept -> rstd::json::Error {
    const auto location = cursor_.source_position();
    return rstd::json::Error(code, location.line, location.column - usize(1));
}

auto JsonReader::consume_ident(ref<str> value) -> Result<empty, rstd::json::Error> {
    for (usize index {}; index < value.size(); ++index) {
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
        if (peek() != value[index]) return Err(error(ErrorCode::ExpectedSomeIdent));
        take();
    }
    return Ok(empty {});
}

auto JsonReader::parse_hex_escape() -> Result<u16, rstd::json::Error> {
    u16 value {};
    for (usize index {}; index < usize(4); ++index) {
        if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
        auto digit = rstd::ascii::digit_value(peek(), u8(16));
        if (digit.is_none()) return Err(error(ErrorCode::InvalidEscape));
        take();
        value = (value << u64(4)) | rstd::as_cast<u16>(*digit);
    }
    return Ok(value);
}

auto JsonReader::parse_unicode_escape(String& output) -> Result<empty, rstd::json::Error> {
    auto first_result = parse_hex_escape();
    if (first_result.is_err()) return Err(rstd::move(first_result).unwrap_err_unchecked());
    const u16 first = first_result.unwrap_unchecked();
    if (first >= u16(0xdc00) && first <= u16(0xdfff)) {
        return Err(error_after_consumed(ErrorCode::LoneLeadingSurrogateInHexEscape));
    }
    if (first < u16(0xd800) || first > u16(0xdbff)) {
        output.push(static_cast<char32_t>(first.to_primitive()));
        return Ok(empty {});
    }
    if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
    if (peek() != u8('\\')) return Err(error(ErrorCode::UnexpectedEndOfHexEscape));
    take();
    if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
    if (peek() != u8('u')) return Err(error(ErrorCode::UnexpectedEndOfHexEscape));
    take();
    auto second_result = parse_hex_escape();
    if (second_result.is_err()) return Err(rstd::move(second_result).unwrap_err_unchecked());
    const u16 second = second_result.unwrap_unchecked();
    if (second < u16(0xdc00) || second > u16(0xdfff)) {
        return Err(error_after_consumed(ErrorCode::LoneLeadingSurrogateInHexEscape));
    }
    const u32 scalar = ((((rstd::as_cast<u32>(first) - u32(0xd800)) << u64(10)) |
                         (rstd::as_cast<u32>(second) - u32(0xdc00))) +
                        u32(0x10000));
    output.push(static_cast<char32_t>(scalar.to_primitive()));
    return Ok(empty {});
}

JsonReader::JsonReader(ref<str> input, rstd::json::ParseOptions options) noexcept
    : cursor_(rstd::parse::text_input(input)), options_(options) {
}

auto JsonReader::value_kind() -> Result<JsonValueKind, rstd::json::Error> {
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
    switch (peek().to_primitive()) {
    case 'n': return Ok(JsonValueKind::Null);
    case 't':
    case 'f': return Ok(JsonValueKind::Boolean);
    case '"': return Ok(JsonValueKind::String);
    case '[': return Ok(JsonValueKind::Array);
    case '{': return Ok(JsonValueKind::Object);
    case '-': return Ok(JsonValueKind::Number);
    default:
        if (peek() >= u8('0') && peek() <= u8('9')) return Ok(JsonValueKind::Number);
        return Err(error(ErrorCode::ExpectedSomeValue));
    }
}

auto JsonReader::parse_null() -> Result<empty, rstd::json::Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != JsonValueKind::Null) return Err(error(ErrorCode::ExpectedSomeValue));
    auto consumed = consume_ident("null"_str);
    if (consumed.is_err()) return Err(rstd::move(consumed).unwrap_err_unchecked());
    return Ok(empty {});
}

auto JsonReader::parse_bool() -> Result<bool, rstd::json::Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != JsonValueKind::Boolean) return Err(error(ErrorCode::ExpectedSomeValue));
    const bool value    = peek() == u8('t');
    auto       consumed = consume_ident(value ? "true"_str : "false"_str);
    if (consumed.is_err()) return Err(rstd::move(consumed).unwrap_err_unchecked());
    return Ok(value);
}

auto JsonReader::parse_string() -> Result<String, rstd::json::Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != JsonValueKind::String) return Err(error(ErrorCode::ExpectedSomeValue));
    take();
    auto output = String::make();
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
            if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err_unchecked());
            break;
        }
        default: return Err(error_after_consumed(ErrorCode::InvalidEscape));
        }
    }
    return Err(error(ErrorCode::EofWhileParsingString));
}

auto JsonReader::parse_float(rstd::parse::Span integer,
                             rstd::parse::Span fraction,
                             i32               exponent,
                             bool negative) -> Result<rstd::json::Number, rstd::json::Error> {
    auto parsed = rstd::num::dec2flt::to_f64({
        .integer  = cursor_.view(integer),
        .fraction = cursor_.view(fraction),
        .exponent = exponent,
        .negative = negative,
    });
    if (parsed.is_err()) {
        const auto cause = parsed.unwrap_err_unchecked();
        return Err(error(cause == rstd::num::dec2flt::Error::Overflow ? ErrorCode::NumberOutOfRange
                                                                      : ErrorCode::InvalidNumber));
    }
    auto number = rstd::json::Number::from_f64(parsed.unwrap_unchecked());
    if (number.is_none()) return Err(error(ErrorCode::NumberOutOfRange));
    return Ok(*number);
}

auto JsonReader::parse_number() -> Result<rstd::json::Number, rstd::json::Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != JsonValueKind::Number) return Err(error(ErrorCode::InvalidNumber));
    bool negative = false;
    if (peek() == u8('-')) {
        negative = true;
        take();
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
    }
    auto integer_begin = cursor_.checkpoint();
    if (peek() == u8('0')) {
        take();
        if (! eof() && rstd::ascii::is_digit(peek())) return Err(error(ErrorCode::InvalidNumber));
    } else if (peek() >= u8('1') && peek() <= u8('9')) {
        rstd::parse::consume_while(cursor_, rstd::parse::ascii::digit);
    } else {
        return Err(error(ErrorCode::InvalidNumber));
    }
    const auto        integer  = cursor_.span_from(integer_begin);
    bool              floating = false;
    rstd::parse::Span fraction { .begin = integer.end, .end = integer.end };
    if (! eof() && peek() == u8('.')) {
        floating = true;
        take();
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
        auto digits = rstd::parse::consume_while_one(cursor_, rstd::parse::ascii::digit);
        if (digits.is_none()) return Err(error(ErrorCode::InvalidNumber));
        fraction = *digits;
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
        auto digits = rstd::parse::consume_while_one(cursor_, rstd::parse::ascii::digit);
        if (digits.is_none()) return Err(error(ErrorCode::InvalidNumber));
        for (auto digit : cursor_.view(*digits)) {
            if (exponent < i32(10'000)) {
                exponent = exponent * i32(10) + rstd::as_cast<i32>(digit - u8('0'));
                if (exponent > i32(10'000)) exponent = i32(10'000);
            }
        }
        if (exponent_negative) exponent = -exponent;
    }
    if (floating) return parse_float(integer, fraction, exponent, negative);
    auto digits_view = cursor_.view(integer);
    u64  magnitude {};
    bool overflow = false;
    for (auto digit : digits_view) {
        const u64 value = rstd::as_cast<u64>(digit - u8('0'));
        if (magnitude > (u64::MAX - value) / u64(10)) {
            overflow = true;
            break;
        }
        magnitude = magnitude * u64(10) + value;
    }
    if (overflow) {
        return parse_float(integer,
                           rstd::parse::Span { .begin = integer.end, .end = integer.end },
                           i32(),
                           negative);
    }
    if (! negative) return Ok(rstd::json::Number::from_u64(magnitude));
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
    const i64 signed_value = magnitude == min_magnitude ? i64::MIN : -rstd::as_cast<i64>(magnitude);
    return Ok(rstd::json::Number::from_i64(signed_value));
}

auto JsonReader::begin_array() -> Result<empty, rstd::json::Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != JsonValueKind::Array) return Err(error(ErrorCode::ExpectedSomeValue));
    if (remaining_depth_ == u8(1)) return Err(error(ErrorCode::RecursionLimitExceeded));
    --remaining_depth_;
    take();
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    return Ok(empty {});
}

auto JsonReader::leave_container(u8 delimiter) noexcept -> void {
    take();
    ++remaining_depth_;
    (void)delimiter;
}

auto JsonReader::next_array(bool& first) -> Result<bool, rstd::json::Error> {
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    if (eof()) return Err(error(ErrorCode::EofWhileParsingList));
    if (peek() == u8(']')) {
        leave_container(u8(']'));
        return Ok(false);
    }
    if (! first) {
        if (peek() != u8(',')) return Err(error(ErrorCode::ExpectedListCommaOrEnd));
        take();
        if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
        if (peek() == u8(']')) return Err(error(ErrorCode::TrailingComma));
    }
    first = false;
    return Ok(true);
}

auto JsonReader::begin_object() -> Result<empty, rstd::json::Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != JsonValueKind::Object) return Err(error(ErrorCode::ExpectedSomeValue));
    if (remaining_depth_ == u8(1)) return Err(error(ErrorCode::RecursionLimitExceeded));
    --remaining_depth_;
    take();
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    return Ok(empty {});
}

auto JsonReader::next_object_key(bool& first) -> Result<Option<String>, rstd::json::Error> {
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    if (eof()) return Err(error(ErrorCode::EofWhileParsingObject));
    if (peek() == u8('}')) {
        leave_container(u8('}'));
        return Ok(None<String>());
    }
    if (! first) {
        if (peek() != u8(',')) return Err(error(ErrorCode::ExpectedObjectCommaOrEnd));
        take();
        if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
        if (peek() == u8('}')) return Err(error(ErrorCode::TrailingComma));
    }
    if (peek() != u8('"')) return Err(error(ErrorCode::KeyMustBeAString));
    auto key = parse_string();
    if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    if (eof()) return Err(error(ErrorCode::EofWhileParsingObject));
    if (peek() != u8(':')) return Err(error(ErrorCode::ExpectedColon));
    take();
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    first = false;
    return Ok(Some(rstd::move(key).unwrap_unchecked()));
}

auto JsonReader::skip_value() -> Result<empty, rstd::json::Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    switch (*kind) {
    case JsonValueKind::Null: return parse_null();
    case JsonValueKind::Boolean: {
        auto value = parse_bool();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(empty {});
    }
    case JsonValueKind::Number: {
        auto value = parse_number();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(empty {});
    }
    case JsonValueKind::String: {
        auto value = parse_string();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(empty {});
    }
    case JsonValueKind::Array: {
        auto begun = begin_array();
        if (begun.is_err()) return begun;
        bool first = true;
        for (;;) {
            auto next = next_array(first);
            if (next.is_err()) return Err(rstd::move(next).unwrap_err_unchecked());
            if (! *next) return Ok(empty {});
            auto skipped = skip_value();
            if (skipped.is_err()) return skipped;
        }
    }
    case JsonValueKind::Object: {
        auto begun = begin_object();
        if (begun.is_err()) return begun;
        bool first = true;
        for (;;) {
            auto key = next_object_key(first);
            if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
            if (key->is_none()) return Ok(empty {});
            auto skipped = skip_value();
            if (skipped.is_err()) return skipped;
        }
    }
    }
    __builtin_unreachable();
}

auto JsonReader::finish() -> Result<empty, rstd::json::Error> {
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    if (! eof()) return Err(error(ErrorCode::TrailingCharacters));
    return Ok(empty {});
}

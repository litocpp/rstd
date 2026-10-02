module rstd.json;
import :reader;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

namespace rstd::json
{

Reader::Reader(ref<str> input, ParseOptions options) noexcept
    : cursor_(rstd::parse::text_input(input)),
      units_(rstd::parse::Input<u16>({})),
      utf16_(false),
      remaining_depth_(options.max_depth),
      options_(options) {
}

Reader::Reader(slice<u16> input, ParseOptions options) noexcept
    : cursor_(rstd::parse::text_input(""_str)),
      units_(rstd::parse::Input<u16>(input)),
      utf16_(true),
      remaining_depth_(options.max_depth),
      options_(options) {
}

auto Reader::position() const noexcept -> usize {
    return utf16_ ? units_.position() : cursor_.position();
}

auto Reader::eof() const noexcept -> bool {
    return utf16_ ? units_.is_eof() : cursor_.is_eof();
}

auto Reader::peek(usize ahead) const noexcept -> u16 {
    if (utf16_) {
        auto value = units_.peek(ahead);
        return value.is_none() ? u16() : value->get();
    }
    auto value = cursor_.peek(ahead);
    return value.is_none() ? u16() : rstd::as_cast<u16>(value->get());
}

auto Reader::take() noexcept -> u16 {
    return utf16_ ? units_.take()->get() : rstd::as_cast<u16>(cursor_.take()->get());
}

auto Reader::unit_at(usize offset) const noexcept -> u16 {
    return utf16_ ? units_.input()[offset] : rstd::as_cast<u16>(cursor_.input()[offset]);
}

auto Reader::consume_whitespace() noexcept -> Option<Error> {
    while (! eof()) {
        switch (peek().to_primitive()) {
        case ' ':
        case '\n':
        case '\r':
        case '\t': take(); break;
        default:
            if (! options_.allow_comments || peek() != u16('/')) return None();
            if (peek(usize(1)) == u16('/')) {
                take();
                take();
                while (! eof() && peek() != u16('\n')) take();
                break;
            }
            if (peek(usize(1)) == u16('*')) {
                take();
                take();
                bool closed = false;
                while (! eof()) {
                    if (peek() == u16('*') && peek(usize(1)) == u16('/')) {
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

auto Reader::error(ErrorCode code) const noexcept -> Error {
    const auto location = utf16_ ? units_.source_position() : cursor_.source_position();
    return Error(
        code, location.line, eof() ? location.column - usize(1) : location.column, position());
}

auto Reader::error_after_consumed(ErrorCode code) const noexcept -> Error {
    const auto location = utf16_ ? units_.source_position() : cursor_.source_position();
    return Error(code, location.line, location.column - usize(1), position() - usize(1));
}

auto Reader::consume_ident(ref<str> value) -> Result<empty, Error> {
    for (usize index {}; index < value.size(); ++index) {
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
        if (peek() != rstd::as_cast<u16>(value[index]))
            return Err(error(ErrorCode::ExpectedSomeIdent));
        take();
    }
    return Ok(empty {});
}

auto Reader::parse_hex_escape() -> Result<u16, Error> {
    u16 value {};
    for (usize index {}; index < usize(4); ++index) {
        if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
        auto digit = rstd::try_from<u8>(peek()).ok().and_then([](u8 value) {
            return rstd::ascii::digit_value(value, u8(16));
        });
        if (digit.is_none()) return Err(error(ErrorCode::InvalidEscape));
        take();
        value = (value << u64(4)) | rstd::as_cast<u16>(*digit);
    }
    return Ok(value);
}

template<typename Output>
auto Reader::parse_unicode_escape(Output& output) -> Result<empty, Error> {
    auto first_result = parse_hex_escape();
    if (first_result.is_err()) return Err(rstd::move(first_result).unwrap_err_unchecked());
    const u16 first = first_result.unwrap_unchecked();
    if constexpr (rstd::mtp::same_as<Output, Vec<u16>>) {
        output.push(u16(first));
        return Ok(empty {});
    } else {
        if (first >= u16(0xdc00) && first <= u16(0xdfff)) {
            return Err(error_after_consumed(ErrorCode::LoneLeadingSurrogateInHexEscape));
        }
        if (first < u16(0xd800) || first > u16(0xdbff)) {
            output.push(static_cast<char32_t>(first.to_primitive()));
            return Ok(empty {});
        }
        if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
        if (peek() != u16('\\')) return Err(error(ErrorCode::UnexpectedEndOfHexEscape));
        take();
        if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
        if (peek() != u16('u')) return Err(error(ErrorCode::UnexpectedEndOfHexEscape));
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
}

auto Reader::value_kind() -> Result<ValueKind, Error> {
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
    switch (peek().to_primitive()) {
    case 'n': return Ok(ValueKind::Null);
    case 't':
    case 'f': return Ok(ValueKind::Boolean);
    case '"': return Ok(ValueKind::String);
    case '[': return Ok(ValueKind::Array);
    case '{': return Ok(ValueKind::Object);
    case '-': return Ok(ValueKind::Number);
    default:
        if (peek() >= u16('0') && peek() <= u16('9')) return Ok(ValueKind::Number);
        return Err(error(ErrorCode::ExpectedSomeValue));
    }
}

auto Reader::parse_null() -> Result<empty, Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != ValueKind::Null) return Err(error(ErrorCode::ExpectedSomeValue));
    auto consumed = consume_ident("null"_str);
    if (consumed.is_err()) return Err(rstd::move(consumed).unwrap_err_unchecked());
    return Ok(empty {});
}

auto Reader::parse_bool() -> Result<bool, Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != ValueKind::Boolean) return Err(error(ErrorCode::ExpectedSomeValue));
    const bool value    = peek() == u16('t');
    auto       consumed = consume_ident(value ? "true"_str : "false"_str);
    if (consumed.is_err()) return Err(rstd::move(consumed).unwrap_err_unchecked());
    return Ok(value);
}

auto Reader::append_utf16(String& output, slice<u16> units) -> Result<empty, Error> {
    for (usize index {}; index < units.len(); ++index) {
        auto value  = units[index];
        u32  scalar = rstd::as_cast<u32>(value);
        if (value >= u16(0xd800) && value <= u16(0xdbff)) {
            if (++index == units.len() || units[index] < u16(0xdc00) || units[index] > u16(0xdfff))
                return Err(error(ErrorCode::InvalidUnicodeCodePoint));
            scalar = u32(0x10000) + ((scalar - u32(0xd800)) << u64(10)) +
                     rstd::as_cast<u32>(units[index] - u16(0xdc00));
        } else if (value >= u16(0xdc00) && value <= u16(0xdfff)) {
            return Err(error(ErrorCode::InvalidUnicodeCodePoint));
        }
        output.push(static_cast<char32_t>(scalar.to_primitive()));
    }
    return Ok(empty {});
}

template<typename Output>
auto Reader::append_raw(Output& output, rstd::parse::Span span) -> Result<empty, Error> {
    if (! utf16_) {
        auto text = cursor_.text(span).unwrap();
        if constexpr (rstd::mtp::same_as<Output, String>) {
            output.push_str(text);
        } else {
            for (auto cp : text.chars()) {
                auto value = u32(static_cast<rstd::uint32_t>(cp.to_primitive()));
                if (value <= u32(0xffff)) {
                    output.push(rstd::as_cast<u16>(value));
                } else {
                    value -= u32(0x10000);
                    output.push(rstd::as_cast<u16>(u32(0xd800) + (value >> u64(10))));
                    output.push(rstd::as_cast<u16>(u32(0xdc00) + (value & u32(0x3ff))));
                }
            }
        }
    } else if constexpr (rstd::mtp::same_as<Output, String>) {
        return append_utf16(output, units_.view(span));
    } else {
        for (auto value : units_.view(span)) output.push(u16(value));
    }
    return Ok(empty {});
}

template<typename Output>
auto Reader::read_string() -> Result<Output, Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != ValueKind::String) return Err(error(ErrorCode::ExpectedSomeValue));
    take();
    auto output = Output::make();
    auto append = [&](char32_t value) {
        if constexpr (rstd::mtp::same_as<Output, String>)
            output.push(value);
        else
            output.push(u16(static_cast<rstd::uint16_t>(value)));
    };
    while (! eof()) {
        auto chunk_start = position();
        while (! eof() && peek() != u16('"') && peek() != u16('\\') && peek() >= u16(0x20)) take();
        auto raw = append_raw(output, { chunk_start, position() });
        if (raw.is_err()) return Err(rstd::move(raw).unwrap_err_unchecked());
        if (eof()) break;
        const u16 byte = peek();
        if (byte == u16('"')) {
            take();
            return Ok(rstd::move(output));
        }
        if (byte < u16(0x20)) {
            take();
            return Err(error_after_consumed(ErrorCode::ControlCharacterWhileParsingString));
        }
        take();
        if (eof()) return Err(error(ErrorCode::EofWhileParsingString));
        switch (take().to_primitive()) {
        case '"': append(U'"'); break;
        case '\\': append(U'\\'); break;
        case '/': append(U'/'); break;
        case 'b': append(U'\b'); break;
        case 'f': append(U'\f'); break;
        case 'n': append(U'\n'); break;
        case 'r': append(U'\r'); break;
        case 't': append(U'\t'); break;
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

auto Reader::parse_string() -> Result<String, Error> {
    if (! utf16_) return read_string<String>();
    auto units = parse_string_units();
    if (units.is_err()) return Err(rstd::move(units).unwrap_err_unchecked());
    auto output  = String::make();
    auto decoded = append_utf16(output, units->as_slice());
    if (decoded.is_err()) return Err(rstd::move(decoded).unwrap_err_unchecked());
    return Ok(rstd::move(output));
}
auto Reader::parse_string_units() -> Result<Vec<u16>, Error> {
    return read_string<Vec<u16>>();
}

auto Reader::scan_number() -> Result<NumberToken, Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != ValueKind::Number) return Err(error(ErrorCode::InvalidNumber));
    NumberToken token {};
    if (peek() == u16('-')) {
        token.negative = true;
        take();
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
    }
    auto digit = [&] {
        return ! eof() && peek() >= u16('0') && peek() <= u16('9');
    };
    token.integer.begin = position();
    if (peek() == u16('0')) {
        take();
        if (digit()) return Err(error(ErrorCode::InvalidNumber));
    } else if (peek() >= u16('1') && peek() <= u16('9')) {
        while (digit()) take();
    } else
        return Err(error(ErrorCode::InvalidNumber));
    token.integer.end = position();
    token.fraction    = { position(), position() };
    if (! eof() && peek() == u16('.')) {
        token.floating = true;
        take();
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
        token.fraction.begin = position();
        while (digit()) take();
        token.fraction.end = position();
        if (token.fraction.is_empty()) return Err(error(ErrorCode::InvalidNumber));
    }
    if (! eof() && (peek() == u16('e') || peek() == u16('E'))) {
        token.floating = true;
        take();
        bool negative = false;
        if (! eof() && (peek() == u16('+') || peek() == u16('-'))) {
            negative = peek() == u16('-');
            take();
        }
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
        if (! digit()) return Err(error(ErrorCode::InvalidNumber));
        while (digit()) {
            auto       value          = take() - u16('0');
            const auto exponent_digit = rstd::as_cast<i32>(value);
            if (token.exponent > (i32::MAX - exponent_digit) / i32(10))
                token.exponent = i32::MAX;
            else
                token.exponent = token.exponent * i32(10) + exponent_digit;
        }
        if (negative) token.exponent = -token.exponent;
    }
    return Ok(token);
}

auto Reader::parse_float(const NumberToken& token, FloatOverflow overflow) -> Result<f64, Error> {
    Vec<u8> integer, fraction;
    auto    bytes = [&](rstd::parse::Span span, Vec<u8>& storage) -> slice<u8> {
        if (! utf16_) return cursor_.view(span);
        for (auto index = span.begin; index < span.end; ++index)
            storage.push(rstd::as_cast<u8>(unit_at(index)));
        return storage.as_slice();
    };
    auto parsed = rstd::num::dec2flt::to_f64({
        .integer  = bytes(token.integer, integer),
        .fraction = bytes(token.fraction, fraction),
        .exponent = token.exponent,
        .negative = token.negative,
    });
    if (parsed.is_err()) {
        const auto cause = parsed.unwrap_err_unchecked();
        if (cause == rstd::num::dec2flt::Error::Overflow) {
            if (overflow == FloatOverflow::Infinity)
                return Ok(token.negative ? f64::NEG_INFINITY : f64::INFINITY_);
            return Err(error(ErrorCode::NumberOutOfRange));
        }
        return Err(error(ErrorCode::InvalidNumber));
    }
    return Ok(parsed.unwrap_unchecked());
}

auto Reader::parse_f64(FloatOverflow overflow) -> Result<f64, Error> {
    auto token = scan_number();
    if (token.is_err()) return Err(rstd::move(token).unwrap_err_unchecked());
    return parse_float(*token, overflow);
}

auto Reader::parse_number() -> Result<Number, Error> {
    auto scanned = scan_number();
    if (scanned.is_err()) return Err(rstd::move(scanned).unwrap_err_unchecked());
    const auto token    = *scanned;
    auto       floating = [&]() -> Result<Number, Error> {
        auto parsed = parse_float(token, FloatOverflow::Reject);
        if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err_unchecked());
        return Ok(Number::from_f64(*parsed).unwrap_unchecked());
    };
    if (token.floating) return floating();
    u64  magnitude {};
    bool overflow = false;
    for (auto index = token.integer.begin; index < token.integer.end; ++index) {
        const u64 value = rstd::as_cast<u64>(unit_at(index) - u16('0'));
        if (magnitude > (u64::MAX - value) / u64(10)) {
            overflow = true;
            break;
        }
        magnitude = magnitude * u64(10) + value;
    }
    if (overflow) {
        if (options_.reject_integer_overflow) return Err(error(ErrorCode::NumberOutOfRange));
        return floating();
    }
    if (! token.negative) return Ok(Number::from_u64(magnitude));
    if (magnitude == u64()) return floating();
    const u64 min_magnitude = rstd::as_cast<u64>(i64::MAX) + u64(1);
    if (magnitude > min_magnitude) {
        if (options_.reject_integer_overflow) return Err(error(ErrorCode::NumberOutOfRange));
        return floating();
    }
    return Ok(
        Number::from_i64(magnitude == min_magnitude ? i64::MIN : -rstd::as_cast<i64>(magnitude)));
}

auto Reader::begin_array() -> Result<empty, Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != ValueKind::Array) return Err(error(ErrorCode::ExpectedSomeValue));
    if (remaining_depth_ <= usize(1)) return Err(error(ErrorCode::RecursionLimitExceeded));
    --remaining_depth_;
    take();
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    return Ok(empty {});
}

auto Reader::leave_container() noexcept -> void {
    take();
    ++remaining_depth_;
}

auto Reader::next_array(bool& first) -> Result<bool, Error> {
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    if (eof()) return Err(error(ErrorCode::EofWhileParsingList));
    if (peek() == u16(']')) {
        leave_container();
        return Ok(false);
    }
    if (! first) {
        if (peek() != u16(',')) return Err(error(ErrorCode::ExpectedListCommaOrEnd));
        take();
        if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
        if (peek() == u16(']')) return Err(error(ErrorCode::TrailingComma));
    }
    first = false;
    return Ok(true);
}

auto Reader::begin_object() -> Result<empty, Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    if (*kind != ValueKind::Object) return Err(error(ErrorCode::ExpectedSomeValue));
    if (remaining_depth_ <= usize(1)) return Err(error(ErrorCode::RecursionLimitExceeded));
    --remaining_depth_;
    take();
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    return Ok(empty {});
}

template<typename Output>
auto Reader::read_object_key(bool& first) -> Result<Option<Output>, Error> {
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    if (eof()) return Err(error(ErrorCode::EofWhileParsingObject));
    if (peek() == u16('}')) {
        leave_container();
        return Ok(None<Output>());
    }
    if (! first) {
        if (peek() != u16(',')) return Err(error(ErrorCode::ExpectedObjectCommaOrEnd));
        take();
        if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
        if (eof()) return Err(error(ErrorCode::EofWhileParsingValue));
        if (peek() == u16('}')) return Err(error(ErrorCode::TrailingComma));
    }
    if (peek() != u16('"')) return Err(error(ErrorCode::KeyMustBeAString));
    auto key = [&] {
        if constexpr (rstd::mtp::same_as<Output, String>)
            return parse_string();
        else
            return parse_string_units();
    }();
    if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    if (eof()) return Err(error(ErrorCode::EofWhileParsingObject));
    if (peek() != u16(':')) return Err(error(ErrorCode::ExpectedColon));
    take();
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    first = false;
    return Ok(Some(rstd::move(key).unwrap_unchecked()));
}

auto Reader::next_object_key(bool& first) -> Result<Option<String>, Error> {
    return read_object_key<String>(first);
}

auto Reader::next_object_key_units(bool& first) -> Result<Option<Vec<u16>>, Error> {
    return read_object_key<Vec<u16>>(first);
}

auto Reader::skip_value() -> Result<empty, Error> {
    auto kind = value_kind();
    if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
    switch (*kind) {
    case ValueKind::Null: return parse_null();
    case ValueKind::Boolean: {
        auto value = parse_bool();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(empty {});
    }
    case ValueKind::Number: {
        auto value = parse_number();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(empty {});
    }
    case ValueKind::String: {
        auto value = parse_string();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err_unchecked());
        return Ok(empty {});
    }
    case ValueKind::Array: {
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
    case ValueKind::Object: {
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
    rstd::unreachable();
}

auto Reader::finish() -> Result<empty, Error> {
    if (auto failure = consume_whitespace(); failure.is_some()) return Err(*failure);
    if (! eof()) return Err(error(ErrorCode::TrailingCharacters));
    return Ok(empty {});
}

} // namespace rstd::json

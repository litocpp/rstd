module rstd.json;
import :parser;
import :reader;

using namespace rstd::prelude;
using namespace rstd::json;
using namespace rstd::literals;

class Parser {
    JsonReader reader_;

    auto parse_array() -> ParseResult {
        auto begun = reader_.begin_array();
        if (begun.is_err()) return Err(rstd::move(begun).unwrap_err_unchecked());
        auto values = Array::make();
        bool first  = true;
        for (;;) {
            auto next = reader_.next_array(first);
            if (next.is_err()) return Err(rstd::move(next).unwrap_err_unchecked());
            if (! *next) return Ok(Value::Array(rstd::move(values)));
            auto value = parse_value();
            if (value.is_err()) return value;
            values.push(rstd::move(value).unwrap_unchecked());
        }
    }

    auto parse_object() -> ParseResult {
        auto begun = reader_.begin_object();
        if (begun.is_err()) return Err(rstd::move(begun).unwrap_err_unchecked());
        auto values = Map::make();
        bool first  = true;
        for (;;) {
            auto key = reader_.next_object_key(first);
            if (key.is_err()) return Err(rstd::move(key).unwrap_err_unchecked());
            if (key->is_none()) return Ok(Value::Object(rstd::move(values)));
            auto value = parse_value();
            if (value.is_err()) return value;
            auto replaced = values.insert(rstd::move(key).unwrap_unchecked().unwrap_unchecked(),
                                          rstd::move(value).unwrap_unchecked());
            if (reader_.reject_duplicate_keys() && replaced.is_some()) {
                return Err(reader_.duplicate_key_error());
            }
        }
    }

public:
    explicit Parser(ref<str> input, ParseOptions options = {}) noexcept: reader_(input, options) {}

    static auto invalid_unicode_error() noexcept -> Error {
        return Error(ErrorCode::InvalidUnicodeCodePoint, usize(1), usize(1));
    }

    auto parse_value() -> ParseResult {
        auto kind = reader_.value_kind();
        if (kind.is_err()) return Err(rstd::move(kind).unwrap_err_unchecked());
        switch (*kind) {
        case JsonValueKind::Null: {
            auto parsed = reader_.parse_null();
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err_unchecked());
            return Ok(Value::Null());
        }
        case JsonValueKind::Boolean: {
            auto parsed = reader_.parse_bool();
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err_unchecked());
            return Ok(Value::Bool(*parsed));
        }
        case JsonValueKind::Number: {
            auto parsed = reader_.parse_number();
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err_unchecked());
            return Ok(Value::Number(*parsed));
        }
        case JsonValueKind::String: {
            auto parsed = reader_.parse_string();
            if (parsed.is_err()) return Err(rstd::move(parsed).unwrap_err_unchecked());
            return Ok(Value::String(rstd::move(parsed).unwrap_unchecked()));
        }
        case JsonValueKind::Array: return parse_array();
        case JsonValueKind::Object: return parse_object();
        }
        rstd::unreachable();
    }

    auto parse() -> ParseResult {
        auto value = parse_value();
        if (value.is_err()) return value;
        auto finished = reader_.finish();
        if (finished.is_err()) return Err(rstd::move(finished).unwrap_err_unchecked());
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
    if (text.is_err()) return Err(Parser::invalid_unicode_error());
    return from_str(rstd::move(text).unwrap_unchecked(), options);
}

} // namespace rstd::json

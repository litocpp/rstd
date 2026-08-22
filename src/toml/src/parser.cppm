export module rstd.toml:parser;
import rstd.alloc;
import rstd.parse;
export import :value;
export import :error;

using ::alloc::boxed::Box;
using ::alloc::collections::BTreeMap;
using ::alloc::string::String;
using ::alloc::vec::Vec;
using namespace rstd::prelude;

export namespace rstd::toml
{

/// The result of parsing a TOML document.
using ParseResult = rstd::Result<Value, Error>;

/// An owned TOML dotted key.
using KeyPath = Vec<String>;

/// One TOML dotted-key assignment.
struct Assignment {
    KeyPath key;
    Value   value;
};

/// One TOML dotted key followed by an unparsed assignment value.
struct AssignmentText {
    KeyPath key;
    String  value;
};

/// Resource limits applied while parsing TOML input.
struct ParseOptions {
    /// The maximum nested array or inline-table depth.
    u8 max_depth { 128 };
    /// The maximum accepted input size in bytes.
    usize max_input_bytes { usize(16 * 1024 * 1024) };
    /// The maximum number of values created by one parse.
    usize max_values { usize(1024 * 1024) };
};

/// Parses a TOML document from UTF-8 text.
auto from_str(ref<str> input) -> ParseResult;
/// Parses a TOML document from UTF-8 text using explicit resource limits.
auto from_str(ref<str> input, ParseOptions options) -> ParseResult;
/// Validates UTF-8 bytes and parses a TOML document.
auto from_slice(slice<u8> input) -> ParseResult;
/// Validates UTF-8 bytes and parses a TOML document using explicit resource limits.
auto from_slice(slice<u8> input, ParseOptions options) -> ParseResult;
/// Parses one TOML dotted key and consumes the complete input.
auto parse_key_path(ref<str> input) -> rstd::Result<KeyPath, Error>;
/// Parses one TOML value and consumes the complete input.
auto parse_value(ref<str> input) -> ParseResult;
/// Parses one TOML dotted-key assignment and consumes the complete input.
auto parse_assignment(ref<str> input) -> rstd::Result<Assignment, Error>;
/// Parses one TOML dotted key and returns the unparsed non-empty assignment value.
auto parse_assignment_text(ref<str> input) -> rstd::Result<AssignmentText, Error>;

} // namespace rstd::toml

using namespace rstd::toml;
using namespace rstd::literals;

using Path  = KeyPath;
using Bytes = Vec<u8>;

void push_scalar(Bytes& output, char32_t value) {
    byte bytes[4] {};
    auto length = rstd::char_::encode_utf8(value, bytes);
    if (length == usize()) rstd::panic("invalid Unicode scalar");
    output.extend_from_slice(slice<u8>::from_raw_parts(bytes, length));
}

auto finish_string(Bytes&& output) -> String {
    return String::from_utf8_unchecked(rstd::move(output));
}

constexpr auto is_horizontal(u8 byte) noexcept -> bool {
    return byte == u8(' ') || byte == u8('\t');
}

constexpr auto is_bare_key(u8 byte) noexcept -> bool {
    return (byte >= u8('a') && byte <= u8('z')) || (byte >= u8('A') && byte <= u8('Z')) ||
           (byte >= u8('0') && byte <= u8('9')) || byte == u8('_') || byte == u8('-');
}

constexpr auto is_digit(u8 byte) noexcept -> bool {
    return byte >= u8('0') && byte <= u8('9');
}

constexpr auto hex_digit(u8 byte) noexcept -> Option<u8> {
    if (byte >= u8('0') && byte <= u8('9')) return Some(byte - u8('0'));
    if (byte >= u8('a') && byte <= u8('f')) return Some(byte - u8('a') + u8(10));
    if (byte >= u8('A') && byte <= u8('F')) return Some(byte - u8('A') + u8(10));
    return None();
}

constexpr auto digit_for_base(u8 byte, u8 base) noexcept -> Option<u8> {
    auto value = hex_digit(byte);
    if (value.is_none() || *value >= base) return None();
    return value;
}

constexpr auto is_leap_year(uint16_t year) noexcept -> bool {
    auto raw = year;
    return raw % 4 == 0 && (raw % 100 != 0 || raw % 400 == 0);
}

constexpr auto days_in_month(uint16_t year, uint8_t month) noexcept -> uint8_t {
    constexpr rstd::uint8_t DAYS[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < uint8_t(1) || month > uint8_t(12)) return uint8_t();
    if (month == uint8_t(2) && is_leap_year(year)) return uint8_t(29);
    return uint8_t(DAYS[month - 1]);
}

enum class DefinitionKind : rstd::uint8_t
{
    ImplicitTable,
    ExplicitTable,
    DottedTable,
    InlineTable,
    ArrayOfTables,
    Value,
};

struct DefinitionNode {
    DefinitionKind                        kind { DefinitionKind::ImplicitTable };
    BTreeMap<String, Box<DefinitionNode>> children {
        BTreeMap<String, Box<DefinitionNode>>::make()
    };
    Vec<Box<DefinitionNode>> array_items { Vec<Box<DefinitionNode>>::make() };

    explicit DefinitionNode(DefinitionKind value) noexcept: kind(value) {}
};

class TomlParser {
    rstd::parse::TextCursor cursor_;
    u8                      remaining_depth_;
    usize                   max_input_bytes_;
    usize                   remaining_values_;
    Table                   root_ { Table::make() };
    DefinitionNode          definitions_ { DefinitionKind::ExplicitTable };
    Path                    current_path_ { Path::make() };

    [[nodiscard]]
    auto eof() const noexcept -> bool {
        return cursor_.is_eof();
    }

    [[nodiscard]]
    auto peek(usize ahead = usize()) const noexcept -> u8 {
        auto value = cursor_.peek(ahead);
        return value.is_none() ? u8() : value->get();
    }

    auto take() noexcept -> u8 { return cursor_.take()->get(); }

    [[nodiscard]]
    auto error(TomlErrorCode code) const noexcept -> Error {
        auto location = cursor_.source_position();
        return Error(code, location.line, location.column, cursor_.position());
    }

public:
    [[nodiscard]]
    static auto invalid_utf8_error() noexcept -> Error {
        return Error(TomlErrorCode::InvalidUtf8, usize(1), usize(1), usize());
    }

private:
    void consume_horizontal() noexcept {
        while (! eof() && is_horizontal(peek())) take();
    }

    auto consume_comment() noexcept -> Option<Error> {
        if (eof() || peek() != u8('#')) return None();
        while (! eof() && peek() != u8('\n')) {
            if (peek() == u8('\r') && peek(usize(1)) == u8('\n')) return None();
            if ((peek() < u8(0x20) && peek() != u8('\t')) || peek() == u8(0x7f)) {
                return Some(error(TomlErrorCode::UnexpectedCharacter));
            }
            take();
        }
        return None();
    }

    auto consume_document_trivia() noexcept -> Option<Error> {
        for (;;) {
            consume_horizontal();
            if (auto failure = consume_comment(); failure.is_some()) return failure;
            if (eof()) return None();
            if (peek() == u8('\n')) {
                take();
                continue;
            }
            if (peek() == u8('\r') && peek(usize(1)) == u8('\n')) {
                take();
                take();
                continue;
            }
            return None();
        }
    }

    auto consume_array_trivia() noexcept -> Option<Error> {
        for (;;) {
            consume_horizontal();
            if (auto failure = consume_comment(); failure.is_some()) return failure;
            if (eof()) return None();
            if (peek() == u8('\n')) {
                take();
                continue;
            }
            if (peek() == u8('\r') && peek(usize(1)) == u8('\n')) {
                take();
                take();
                continue;
            }
            return None();
        }
    }

    [[nodiscard]]
    auto finish_line() -> Option<Error> {
        consume_horizontal();
        if (auto failure = consume_comment(); failure.is_some()) return failure;
        if (eof()) return None();
        if (peek() == u8('\n')) {
            take();
            return None();
        }
        if (peek() == u8('\r') && peek(usize(1)) == u8('\n')) {
            take();
            take();
            return None();
        }
        return Some(error(TomlErrorCode::ExpectedLineEnd));
    }

    [[nodiscard]]
    auto parse_unicode_escape(usize digits) -> Result<char32_t, Error> {
        rstd::uint32_t value {};
        for (usize index {}; index < digits; ++index) {
            if (eof()) return Err(error(TomlErrorCode::UnexpectedEnd));
            auto digit = hex_digit(peek());
            if (digit.is_none()) return Err(error(TomlErrorCode::InvalidUnicode));
            take();
            value = value * 16 + digit->to_primitive();
        }
        if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
            return Err(error(TomlErrorCode::InvalidUnicode));
        }
        return Ok(static_cast<char32_t>(value));
    }

    [[nodiscard]]
    auto parse_basic_string(bool allow_multiline = true) -> Result<String, Error> {
        const bool multiline =
            allow_multiline && peek(usize(1)) == u8('"') && peek(usize(2)) == u8('"');
        take();
        if (multiline) {
            take();
            take();
            if (! eof() && peek() == u8('\n'))
                take();
            else if (! eof() && peek() == u8('\r') && peek(usize(1)) == u8('\n')) {
                take();
                take();
            }
        }

        auto output = Bytes::make();
        while (! eof()) {
            if (peek() == u8('"')) {
                if (! multiline) {
                    take();
                    return Ok(finish_string(rstd::move(output)));
                }
                usize quotes {};
                while (peek(quotes) == u8('"')) ++quotes;
                if (quotes > usize(5)) return Err(error(TomlErrorCode::InvalidString));
                if (quotes >= usize(3) && quotes <= usize(5)) {
                    for (usize index {}; index < quotes - usize(3); ++index) {
                        output.push(u8('"'));
                    }
                    for (usize index {}; index < quotes; ++index) take();
                    return Ok(finish_string(rstd::move(output)));
                }
                output.push(take());
                continue;
            }

            if (peek() == u8('\\')) {
                take();
                if (eof()) return Err(error(TomlErrorCode::UnexpectedEnd));
                usize continuation {};
                while (is_horizontal(peek(continuation))) ++continuation;
                const auto continuation_line =
                    peek(continuation) == u8('\n') ||
                    (peek(continuation) == u8('\r') && peek(continuation + usize(1)) == u8('\n'));
                if (multiline && continuation_line) {
                    while (is_horizontal(peek())) take();
                    if (peek() == u8('\r')) {
                        take();
                        if (eof() || peek() != u8('\n')) {
                            return Err(error(TomlErrorCode::InvalidString));
                        }
                    }
                    take();
                    for (;;) {
                        while (! eof() && is_horizontal(peek())) take();
                        if (! eof() && peek() == u8('\n')) {
                            take();
                            continue;
                        }
                        if (! eof() && peek() == u8('\r') && peek(usize(1)) == u8('\n')) {
                            take();
                            take();
                            continue;
                        }
                        break;
                    }
                    continue;
                }
                const auto escaped = take();
                switch (escaped.to_primitive()) {
                case 'b': output.push(u8('\b')); break;
                case 't': output.push(u8('\t')); break;
                case 'n': output.push(u8('\n')); break;
                case 'f': output.push(u8('\f')); break;
                case 'r': output.push(u8('\r')); break;
                case 'e': output.push(u8(0x1b)); break;
                case '"': output.push(u8('"')); break;
                case '\\': output.push(u8('\\')); break;
                case 'x': {
                    auto scalar = parse_unicode_escape(usize(2));
                    if (scalar.is_err()) return Err(scalar.unwrap_err());
                    push_scalar(output, scalar.unwrap());
                    break;
                }
                case 'u': {
                    auto scalar = parse_unicode_escape(usize(4));
                    if (scalar.is_err()) return Err(scalar.unwrap_err());
                    push_scalar(output, scalar.unwrap());
                    break;
                }
                case 'U': {
                    auto scalar = parse_unicode_escape(usize(8));
                    if (scalar.is_err()) return Err(scalar.unwrap_err());
                    push_scalar(output, scalar.unwrap());
                    break;
                }
                default: return Err(error(TomlErrorCode::InvalidEscape));
                }
                continue;
            }

            if (! multiline && (peek() == u8('\n') || peek() == u8('\r'))) {
                return Err(error(TomlErrorCode::InvalidString));
            }
            if (multiline && peek() == u8('\n')) {
                output.push(take());
                continue;
            }
            if (multiline && peek() == u8('\r') && peek(usize(1)) == u8('\n')) {
                take();
                take();
                output.push(u8('\n'));
                continue;
            }
            if ((peek() < u8(0x20) && peek() != u8('\t')) || peek() == u8(0x7f)) {
                return Err(error(TomlErrorCode::InvalidString));
            }
            output.push(take());
        }
        return Err(error(TomlErrorCode::UnexpectedEnd));
    }

    [[nodiscard]]
    auto parse_literal_string(bool allow_multiline = true) -> Result<String, Error> {
        const bool multiline =
            allow_multiline && peek(usize(1)) == u8('\'') && peek(usize(2)) == u8('\'');
        take();
        if (multiline) {
            take();
            take();
            if (! eof() && peek() == u8('\n'))
                take();
            else if (! eof() && peek() == u8('\r') && peek(usize(1)) == u8('\n')) {
                take();
                take();
            }
        }

        auto output = Bytes::make();
        while (! eof()) {
            if (peek() == u8('\'')) {
                if (! multiline) {
                    take();
                    return Ok(finish_string(rstd::move(output)));
                }
                usize quotes {};
                while (peek(quotes) == u8('\'')) ++quotes;
                if (quotes > usize(5)) return Err(error(TomlErrorCode::InvalidString));
                if (quotes >= usize(3) && quotes <= usize(5)) {
                    for (usize index {}; index < quotes - usize(3); ++index) {
                        output.push(u8('\''));
                    }
                    for (usize index {}; index < quotes; ++index) take();
                    return Ok(finish_string(rstd::move(output)));
                }
            }
            if (! multiline && (peek() == u8('\n') || peek() == u8('\r'))) {
                return Err(error(TomlErrorCode::InvalidString));
            }
            if (multiline && peek() == u8('\n')) {
                output.push(take());
                continue;
            }
            if (multiline && peek() == u8('\r') && peek(usize(1)) == u8('\n')) {
                take();
                take();
                output.push(u8('\n'));
                continue;
            }
            if ((peek() < u8(0x20) && peek() != u8('\t')) || peek() == u8(0x7f)) {
                return Err(error(TomlErrorCode::InvalidString));
            }
            output.push(take());
        }
        return Err(error(TomlErrorCode::UnexpectedEnd));
    }

    [[nodiscard]]
    auto parse_key_part() -> Result<String, Error> {
        if (eof()) return Err(error(TomlErrorCode::ExpectedKey));
        if (peek() == u8('"')) return parse_basic_string(false);
        if (peek() == u8('\'')) return parse_literal_string(false);
        if (! is_bare_key(peek())) return Err(error(TomlErrorCode::ExpectedKey));
        auto begin = cursor_.checkpoint();
        while (! eof() && is_bare_key(peek())) take();
        return Ok(String::make(cursor_.consumed_text(begin)));
    }

    [[nodiscard]]
    auto parse_key_path() -> Result<Path, Error> {
        auto path = Path::make();
        for (;;) {
            consume_horizontal();
            auto key = parse_key_part();
            if (key.is_err()) return Err(key.unwrap_err());
            path.push(key.unwrap());
            consume_horizontal();
            if (eof() || peek() != u8('.')) return Ok(rstd::move(path));
            take();
        }
    }

    [[nodiscard]]
    auto resolve_table(Table& start, const Path& path, usize count) -> Result<Table*, Error> {
        Table* table = &start;
        for (usize index {}; index < count; ++index) {
            auto value = table->get_mut(path[index].as_str());
            if (value.is_none()) {
                table->insert(path[index].clone(), Value::Table(Table::make()));
                value = table->get_mut(path[index].as_str());
            }
            auto* item = rstd::addressof(**value);
            if (item->is_Table()) {
                table = rstd::addressof(item->as_Table().value);
                continue;
            }
            if (item->is_Array()) {
                auto& array = item->as_Array().value;
                if (array.len() == usize() || ! array[array.len() - usize(1)].is_Table()) {
                    return Err(error(TomlErrorCode::TableRedefinition));
                }
                table = rstd::addressof(array[array.len() - usize(1)].as_Table().value);
                continue;
            }
            return Err(error(TomlErrorCode::TableRedefinition));
        }
        return Ok(table);
    }

    [[nodiscard]]
    auto resolve_current_table() -> Result<Table*, Error> {
        return resolve_table(root_, current_path_, current_path_.len());
    }

    [[nodiscard]]
    auto resolve_definition(DefinitionNode& start,
                            const Path&     path,
                            usize           count,
                            DefinitionKind  missing_kind,
                            bool create_missing = true) -> Result<DefinitionNode*, Error> {
        DefinitionNode* table = rstd::addressof(start);
        for (usize index {}; index < count; ++index) {
            auto child = table->children.get_mut(path[index].as_str());
            if (child.is_none()) {
                if (! create_missing) {
                    return Err(error(TomlErrorCode::TableRedefinition));
                }
                table->children.insert(path[index].clone(),
                                       Box<DefinitionNode>::make(missing_kind));
                child = table->children.get_mut(path[index].as_str());
            }
            auto* node = (**child).get();
            if (missing_kind == DefinitionKind::DottedTable &&
                node->kind != DefinitionKind::DottedTable) {
                return Err(error(TomlErrorCode::TableRedefinition));
            }
            if (node->kind == DefinitionKind::ArrayOfTables) {
                if (node->array_items.is_empty()) {
                    return Err(error(TomlErrorCode::TableRedefinition));
                }
                table = node->array_items[node->array_items.len() - usize(1)].get();
                continue;
            }
            if (node->kind != DefinitionKind::ImplicitTable &&
                node->kind != DefinitionKind::ExplicitTable &&
                node->kind != DefinitionKind::DottedTable) {
                return Err(error(TomlErrorCode::TableRedefinition));
            }
            table = node;
        }
        return Ok(table);
    }

    [[nodiscard]]
    auto resolve_current_definition() -> Result<DefinitionNode*, Error> {
        return resolve_definition(
            definitions_, current_path_, current_path_.len(), DefinitionKind::ImplicitTable, false);
    }

    [[nodiscard]]
    auto parse_integer(ref<str> token) -> ParseResult {
        auto normalized = Bytes::with_capacity(token.size());
        for (usize index {}; index < token.size(); ++index) {
            if (token[index] != u8('_')) normalized.push(token[index]);
        }
        auto  text = rstd::str_::from_utf8_unchecked(normalized.as_slice());
        usize index {};
        bool  negative {};
        bool  signed_value {};
        if (text[index] == u8('+') || text[index] == u8('-')) {
            signed_value = true;
            negative     = text[index] == u8('-');
            ++index;
            if (index == text.size()) return Err(error(TomlErrorCode::InvalidNumber));
        }

        u8 base { 10 };
        if (index + usize(2) <= text.size() && text[index] == u8('0')) {
            const auto prefix = text[index + usize(1)];
            if (prefix == u8('x'))
                base = u8(16);
            else if (prefix == u8('o'))
                base = u8(8);
            else if (prefix == u8('b'))
                base = u8(2);
            if (base != u8(10)) index += usize(2);
        }
        if (index == text.size()) return Err(error(TomlErrorCode::InvalidNumber));
        if (base != u8(10) && signed_value) return Err(error(TomlErrorCode::InvalidNumber));
        if (base == u8(10) && text[index] == u8('0') && index + usize(1) < text.size()) {
            return Err(error(TomlErrorCode::InvalidNumber));
        }

        rstd::uint64_t       value {};
        const rstd::uint64_t limit = negative ? 0x8000000000000000ULL : 0x7fffffffffffffffULL;
        for (; index < text.size(); ++index) {
            auto digit = digit_for_base(text[index], base);
            if (digit.is_none()) return Err(error(TomlErrorCode::InvalidNumber));
            const auto raw = static_cast<rstd::uint64_t>(digit->to_primitive());
            if (value > (limit - raw) / base.to_primitive()) {
                return Err(error(TomlErrorCode::NumberOutOfRange));
            }
            value = value * base.to_primitive() + raw;
        }
        if (negative) {
            if (value == 0x8000000000000000ULL) return Ok(Value::Integer(i64::MIN));
            return Ok(Value::Integer(i64(-static_cast<rstd::int64_t>(value))));
        }
        return Ok(Value::Integer(i64(static_cast<rstd::int64_t>(value))));
    }

    [[nodiscard]]
    auto parse_float(ref<str> token) -> ParseResult {
        if (token == "inf"_str || token == "+inf"_str) {
            return Ok(Value::Float(f64::INFINITY_));
        }
        if (token == "-inf"_str) return Ok(Value::Float(f64::NEG_INFINITY));
        if (token == "nan"_str || token == "+nan"_str) {
            return Ok(Value::Float(f64::NAN_));
        }
        if (token == "-nan"_str) return Ok(Value::Float(-f64::NAN_));

        auto normalized = Bytes::with_capacity(token.size());
        for (usize index {}; index < token.size(); ++index) {
            if (token[index] != u8('_')) normalized.push(token[index]);
        }
        auto  text = rstd::str_::from_utf8_unchecked(normalized.as_slice());
        usize cursor {};
        bool  negative {};
        if (text[cursor] == u8('+') || text[cursor] == u8('-')) {
            negative = text[cursor] == u8('-');
            ++cursor;
        }
        const usize integer_begin = cursor;
        while (cursor < text.size() && is_digit(text[cursor])) ++cursor;
        const usize integer_end = cursor;
        if (integer_end == integer_begin ||
            (integer_end - integer_begin > usize(1) && text[integer_begin] == u8('0'))) {
            return Err(error(TomlErrorCode::InvalidNumber));
        }

        usize fraction_begin = cursor;
        usize fraction_end   = cursor;
        if (cursor < text.size() && text[cursor] == u8('.')) {
            ++cursor;
            fraction_begin = cursor;
            while (cursor < text.size() && is_digit(text[cursor])) ++cursor;
            fraction_end = cursor;
            if (fraction_begin == fraction_end) return Err(error(TomlErrorCode::InvalidNumber));
        }

        i32  exponent {};
        bool exponent_present {};
        if (cursor < text.size() && (text[cursor] == u8('e') || text[cursor] == u8('E'))) {
            exponent_present = true;
            ++cursor;
            bool exponent_negative {};
            if (cursor < text.size() && (text[cursor] == u8('+') || text[cursor] == u8('-'))) {
                exponent_negative = text[cursor] == u8('-');
                ++cursor;
            }
            if (cursor == text.size() || ! is_digit(text[cursor])) {
                return Err(error(TomlErrorCode::InvalidNumber));
            }
            rstd::int32_t raw_exponent {};
            while (cursor < text.size() && is_digit(text[cursor])) {
                if (raw_exponent < 100000) {
                    raw_exponent = raw_exponent * 10 + static_cast<rstd::int32_t>(
                                                           (text[cursor] - u8('0')).to_primitive());
                }
                ++cursor;
            }
            exponent = i32(exponent_negative ? -raw_exponent : raw_exponent);
        }
        if (cursor != text.size() || (fraction_begin == fraction_end && ! exponent_present)) {
            return Err(error(TomlErrorCode::InvalidNumber));
        }

        auto result = rstd::num::dec2flt::to_f64({
            .integer  = slice<u8>::from_raw_parts(text.data() + integer_begin.to_primitive(),
                                                  integer_end - integer_begin),
            .fraction = slice<u8>::from_raw_parts(text.data() + fraction_begin.to_primitive(),
                                                  fraction_end - fraction_begin),
            .exponent = exponent,
            .negative = negative,
        });
        if (result.is_err()) {
            return Err(error(result.unwrap_err() == rstd::num::dec2flt::Error::Overflow
                                 ? TomlErrorCode::NumberOutOfRange
                                 : TomlErrorCode::InvalidNumber));
        }
        return Ok(Value::Float(result.unwrap()));
    }

    [[nodiscard]]
    static auto parse_two(ref<str> token, usize offset) noexcept -> Option<uint8_t> {
        if (offset + usize(2) > token.size() || ! is_digit(token[offset]) ||
            ! is_digit(token[offset + usize(1)])) {
            return None();
        }
        return Some(uint8_t((token[offset] - u8('0')).to_primitive() * 10 +
                            (token[offset + usize(1)] - u8('0')).to_primitive()));
    }

    [[nodiscard]]
    static auto parse_four(ref<str> token, usize offset) noexcept -> Option<uint16_t> {
        if (offset + usize(4) > token.size()) return None();
        rstd::uint16_t value {};
        for (usize index {}; index < usize(4); ++index) {
            if (! is_digit(token[offset + index])) return None();
            value = static_cast<rstd::uint16_t>(value * 10 +
                                                (token[offset + index] - u8('0')).to_primitive());
        }
        return Some(uint16_t(value));
    }

    [[nodiscard]]
    auto parse_date(ref<str> token, usize offset) -> Result<LocalDate, Error> {
        auto year  = parse_four(token, offset);
        auto month = parse_two(token, offset + usize(5));
        auto day   = parse_two(token, offset + usize(8));
        if (year.is_none() || month.is_none() || day.is_none() ||
            token[offset + usize(4)] != u8('-') || token[offset + usize(7)] != u8('-') ||
            *month < uint8_t(1) || *month > uint8_t(12) || *day < uint8_t(1) ||
            *day > days_in_month(*year, *month)) {
            return Err(error(TomlErrorCode::InvalidDateTime));
        }
        return Ok(LocalDate { *year, *month, *day });
    }

    [[nodiscard]]
    auto parse_time(ref<str> token, usize offset, usize& end) -> Result<LocalTime, Error> {
        auto hour   = parse_two(token, offset);
        auto minute = parse_two(token, offset + usize(3));
        if (hour.is_none() || minute.is_none() || token[offset + usize(2)] != u8(':') ||
            *hour > uint8_t(23) || *minute > uint8_t(59)) {
            return Err(error(TomlErrorCode::InvalidDateTime));
        }
        uint8_t second {};
        bool    has_seconds {};
        end = offset + usize(5);
        if (end < token.size() && token[end] == u8(':')) {
            auto parsed_second = parse_two(token, end + usize(1));
            if (parsed_second.is_none() || *parsed_second > uint8_t(59)) {
                return Err(error(TomlErrorCode::InvalidDateTime));
            }
            second      = *parsed_second;
            has_seconds = true;
            end += usize(3);
        }
        uint32_t nanosecond {};
        if (end < token.size() && token[end] == u8('.')) {
            if (! has_seconds) return Err(error(TomlErrorCode::InvalidDateTime));
            ++end;
            usize digits {};
            while (end < token.size() && is_digit(token[end])) {
                if (digits < usize(9)) {
                    nanosecond =
                        nanosecond * uint32_t(10) + uint32_t((token[end] - u8('0')).to_primitive());
                }
                ++digits;
                ++end;
            }
            if (digits == usize()) return Err(error(TomlErrorCode::InvalidDateTime));
            while (digits < usize(9)) {
                nanosecond *= uint32_t(10);
                ++digits;
            }
        }
        return Ok(LocalTime { *hour, *minute, second, nanosecond });
    }

    [[nodiscard]]
    auto parse_datetime(ref<str> token) -> ParseResult {
        if (token.size() >= usize(10) && token[usize(4)] == u8('-') && token[usize(7)] == u8('-')) {
            auto date = parse_date(token, usize());
            if (date.is_err()) return Err(date.unwrap_err());
            if (token.size() == usize(10)) return Ok(Value::LocalDate(date.unwrap()));
            if (token.size() < usize(16) ||
                (token[usize(10)] != u8('T') && token[usize(10)] != u8('t') &&
                 token[usize(10)] != u8(' '))) {
                return Err(error(TomlErrorCode::InvalidDateTime));
            }
            usize end {};
            auto  time = parse_time(token, usize(11), end);
            if (time.is_err()) return Err(time.unwrap_err());
            LocalDateTime local { date.unwrap(), time.unwrap() };
            if (end == token.size()) return Ok(Value::LocalDateTime(local));
            if (token[end] == u8('Z') || token[end] == u8('z')) {
                if (end + usize(1) != token.size()) {
                    return Err(error(TomlErrorCode::InvalidDateTime));
                }
                return Ok(Value::OffsetDateTime(OffsetDateTime { local, int16_t() }));
            }
            if (token[end] != u8('+') && token[end] != u8('-')) {
                return Err(error(TomlErrorCode::InvalidDateTime));
            }
            const bool negative = token[end] == u8('-');
            auto       hour     = parse_two(token, end + usize(1));
            auto       minute   = parse_two(token, end + usize(4));
            if (hour.is_none() || minute.is_none() || end + usize(6) != token.size() ||
                token[end + usize(3)] != u8(':') || *hour > uint8_t(23) || *minute > uint8_t(59)) {
                return Err(error(TomlErrorCode::InvalidDateTime));
            }
            rstd::int16_t minutes = static_cast<rstd::int16_t>(*hour * 60 + *minute);
            if (negative) minutes = static_cast<rstd::int16_t>(-minutes);
            return Ok(Value::OffsetDateTime(OffsetDateTime { local, int16_t(minutes) }));
        }
        if (token.size() >= usize(5) && token[usize(2)] == u8(':')) {
            usize end {};
            auto  time = parse_time(token, usize(), end);
            if (time.is_err() || end != token.size()) {
                return Err(error(TomlErrorCode::InvalidDateTime));
            }
            return Ok(Value::LocalTime(time.unwrap()));
        }
        return Err(error(TomlErrorCode::InvalidDateTime));
    }

    [[nodiscard]]
    static auto valid_underscores(ref<str> token) noexcept -> bool {
        usize start {};
        if (! token.is_empty() && (token[usize()] == u8('+') || token[usize()] == u8('-'))) {
            start = usize(1);
        }
        u8 base { 10 };
        if (start + usize(2) <= token.size() && token[start] == u8('0')) {
            const auto prefix = token[start + usize(1)];
            if (prefix == u8('x'))
                base = u8(16);
            else if (prefix == u8('o'))
                base = u8(8);
            else if (prefix == u8('b'))
                base = u8(2);
        }
        for (usize index {}; index < token.size(); ++index) {
            if (token[index] != u8('_')) continue;
            if (index == usize() || index + usize(1) == token.size() ||
                digit_for_base(token[index - usize(1)], base).is_none() ||
                digit_for_base(token[index + usize(1)], base).is_none()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]]
    auto parse_scalar() -> ParseResult {
        auto begin = cursor_.checkpoint();
        while (! eof() && peek() != u8(',') && peek() != u8(']') && peek() != u8('}') &&
               peek() != u8('#') && peek() != u8('\n') && peek() != u8('\r')) {
            take();
        }
        auto span = cursor_.span_from(begin);
        while (! span.is_empty() && is_horizontal(cursor_.input()[span.end - usize(1)])) --span.end;
        if (span.is_empty()) return Err(error(TomlErrorCode::ExpectedValue));
        auto token = cursor_.text(span);
        if (token == "true"_str) return Ok(Value::Boolean(true));
        if (token == "false"_str) return Ok(Value::Boolean(false));
        if ((token.size() >= usize(5) && token[usize(2)] == u8(':')) ||
            (token.size() >= usize(10) && token[usize(4)] == u8('-'))) {
            return parse_datetime(token);
        }
        if (! valid_underscores(token)) return Err(error(TomlErrorCode::InvalidNumber));
        usize numeric_start {};
        if (token[numeric_start] == u8('+') || token[numeric_start] == u8('-')) {
            ++numeric_start;
        }
        if (numeric_start + usize(2) <= token.size() && token[numeric_start] == u8('0') &&
            (token[numeric_start + usize(1)] == u8('x') ||
             token[numeric_start + usize(1)] == u8('o') ||
             token[numeric_start + usize(1)] == u8('b'))) {
            return parse_integer(token);
        }
        bool is_float = false;
        for (usize index {}; index < token.size(); ++index) {
            if (token[index] == u8('.') || token[index] == u8('e') || token[index] == u8('E')) {
                is_float = true;
                break;
            }
        }
        if (token == "inf"_str || token == "+inf"_str || token == "-inf"_str ||
            token == "nan"_str || token == "+nan"_str || token == "-nan"_str) {
            is_float = true;
        }
        return is_float ? parse_float(token) : parse_integer(token);
    }

    [[nodiscard]]
    auto parse_array() -> ParseResult {
        if (remaining_depth_ == u8(1)) {
            return Err(error(TomlErrorCode::RecursionLimitExceeded));
        }
        --remaining_depth_;
        take();
        if (auto failure = consume_array_trivia(); failure.is_some()) return Err(*failure);
        auto values = Array::make();
        if (! eof() && peek() == u8(']')) {
            take();
            ++remaining_depth_;
            return Ok(Value::Array(rstd::move(values)));
        }
        for (;;) {
            auto value = parse_value();
            if (value.is_err()) return Err(value.unwrap_err());
            values.push(value.unwrap());
            if (auto failure = consume_array_trivia(); failure.is_some()) return Err(*failure);
            if (eof()) return Err(error(TomlErrorCode::UnexpectedEnd));
            if (peek() == u8(']')) {
                take();
                ++remaining_depth_;
                return Ok(Value::Array(rstd::move(values)));
            }
            if (peek() != u8(',')) return Err(error(TomlErrorCode::ExpectedCommaOrEnd));
            take();
            if (auto failure = consume_array_trivia(); failure.is_some()) return Err(*failure);
            if (! eof() && peek() == u8(']')) {
                take();
                ++remaining_depth_;
                return Ok(Value::Array(rstd::move(values)));
            }
        }
    }

    [[nodiscard]]
    auto insert_path(Table& table, DefinitionNode& definition, Path& path, Value value)
        -> Option<Error> {
        auto definition_parent = resolve_definition(
            definition, path, path.len() - usize(1), DefinitionKind::DottedTable);
        if (definition_parent.is_err()) return Some(definition_parent.unwrap_err());
        auto parent = resolve_table(table, path, path.len() - usize(1));
        if (parent.is_err()) return Some(parent.unwrap_err());
        auto& key = path[path.len() - usize(1)];
        if (definition_parent.unwrap()->children.get(key.as_str()).is_some() ||
            parent.unwrap()->get(key.as_str()).is_some()) {
            return Some(error(TomlErrorCode::DuplicateKey));
        }
        const auto kind = value.is_Table() ? DefinitionKind::InlineTable : DefinitionKind::Value;
        definition_parent.unwrap()->children.insert(key.clone(), Box<DefinitionNode>::make(kind));
        parent.unwrap()->insert(key.clone(), rstd::move(value));
        return None();
    }

    [[nodiscard]]
    auto parse_inline_table() -> ParseResult {
        if (remaining_depth_ == u8(1)) {
            return Err(error(TomlErrorCode::RecursionLimitExceeded));
        }
        --remaining_depth_;
        take();
        if (auto failure = consume_array_trivia(); failure.is_some()) return Err(*failure);
        auto table      = Table::make();
        auto definition = DefinitionNode(DefinitionKind::InlineTable);
        if (! eof() && peek() == u8('}')) {
            take();
            ++remaining_depth_;
            return Ok(Value::Table(rstd::move(table)));
        }
        for (;;) {
            auto path = parse_key_path();
            if (path.is_err()) return Err(path.unwrap_err());
            if (auto failure = consume_array_trivia(); failure.is_some()) return Err(*failure);
            if (eof() || peek() != u8('=')) return Err(error(TomlErrorCode::ExpectedEquals));
            take();
            if (auto failure = consume_array_trivia(); failure.is_some()) return Err(*failure);
            auto value = parse_value();
            if (value.is_err()) return Err(value.unwrap_err());
            auto owned_path = path.unwrap();
            if (auto failure = insert_path(table, definition, owned_path, value.unwrap());
                failure.is_some()) {
                return Err(*failure);
            }
            if (auto failure = consume_array_trivia(); failure.is_some()) return Err(*failure);
            if (eof()) return Err(error(TomlErrorCode::UnexpectedEnd));
            if (peek() == u8('}')) {
                take();
                ++remaining_depth_;
                return Ok(Value::Table(rstd::move(table)));
            }
            if (peek() != u8(',')) return Err(error(TomlErrorCode::ExpectedCommaOrEnd));
            take();
            if (auto failure = consume_array_trivia(); failure.is_some()) return Err(*failure);
            if (! eof() && peek() == u8('}')) {
                take();
                ++remaining_depth_;
                return Ok(Value::Table(rstd::move(table)));
            }
        }
    }

    [[nodiscard]]
    auto parse_value() -> ParseResult {
        if (eof()) return Err(error(TomlErrorCode::ExpectedValue));
        if (remaining_values_ == usize()) {
            return Err(error(TomlErrorCode::ResourceLimitExceeded));
        }
        --remaining_values_;
        switch (peek().to_primitive()) {
        case '"': {
            auto value = parse_basic_string();
            if (value.is_err()) return Err(value.unwrap_err());
            return Ok(Value::String(value.unwrap()));
        }
        case '\'': {
            auto value = parse_literal_string();
            if (value.is_err()) return Err(value.unwrap_err());
            return Ok(Value::String(value.unwrap()));
        }
        case '[': return parse_array();
        case '{': return parse_inline_table();
        default: return parse_scalar();
        }
    }

    [[nodiscard]]
    auto parse_assignment() -> Option<Error> {
        auto path = parse_key_path();
        if (path.is_err()) return Some(path.unwrap_err());
        consume_horizontal();
        if (eof() || peek() != u8('=')) return Some(error(TomlErrorCode::ExpectedEquals));
        take();
        consume_horizontal();
        auto value = parse_value();
        if (value.is_err()) return Some(value.unwrap_err());
        auto current = resolve_current_table();
        if (current.is_err()) return Some(current.unwrap_err());
        auto current_definition = resolve_current_definition();
        if (current_definition.is_err()) return Some(current_definition.unwrap_err());
        auto owned_path = path.unwrap();
        if (auto failure = insert_path(
                *current.unwrap(), *current_definition.unwrap(), owned_path, value.unwrap());
            failure.is_some()) {
            return failure;
        }
        return finish_line();
    }

    [[nodiscard]]
    auto parse_header() -> Option<Error> {
        take();
        const bool array = ! eof() && peek() == u8('[');
        if (array) take();
        consume_horizontal();
        auto path = parse_key_path();
        if (path.is_err()) return Some(path.unwrap_err());
        consume_horizontal();
        if (eof() || peek() != u8(']')) return Some(error(TomlErrorCode::UnexpectedCharacter));
        take();
        if (array) {
            if (eof() || peek() != u8(']')) {
                return Some(error(TomlErrorCode::UnexpectedCharacter));
            }
            take();
        }

        auto owned_path        = path.unwrap();
        auto definition_parent = resolve_definition(
            definitions_, owned_path, owned_path.len() - usize(1), DefinitionKind::ImplicitTable);
        if (definition_parent.is_err()) return Some(definition_parent.unwrap_err());
        auto parent = resolve_table(root_, owned_path, owned_path.len() - usize(1));
        if (parent.is_err()) return Some(parent.unwrap_err());
        auto& key                 = owned_path[owned_path.len() - usize(1)];
        auto  existing            = parent.unwrap()->get_mut(key.as_str());
        auto  definition_existing = definition_parent.unwrap()->children.get_mut(key.as_str());
        if (array) {
            if (definition_existing.is_none()) {
                auto definition = Box<DefinitionNode>::make(DefinitionKind::ArrayOfTables);
                definition->array_items.push(
                    Box<DefinitionNode>::make(DefinitionKind::ExplicitTable));
                definition_parent.unwrap()->children.insert(key.clone(), rstd::move(definition));
                auto items = Array::make();
                items.push(Value::Table(Table::make()));
                parent.unwrap()->insert(key.clone(), Value::Array(rstd::move(items)));
            } else if ((**definition_existing).get()->kind == DefinitionKind::ArrayOfTables &&
                       existing.is_some() && (**existing).is_Array()) {
                (**definition_existing)
                    .get()
                    ->array_items.push(Box<DefinitionNode>::make(DefinitionKind::ExplicitTable));
                (**existing).as_Array().value.push(Value::Table(Table::make()));
            } else {
                return Some(error(TomlErrorCode::TableRedefinition));
            }
        } else {
            if (definition_existing.is_none()) {
                definition_parent.unwrap()->children.insert(
                    key.clone(), Box<DefinitionNode>::make(DefinitionKind::ExplicitTable));
                parent.unwrap()->insert(key.clone(), Value::Table(Table::make()));
            } else if ((**definition_existing).get()->kind == DefinitionKind::ImplicitTable &&
                       existing.is_some() && (**existing).is_Table()) {
                (**definition_existing).get()->kind = DefinitionKind::ExplicitTable;
            } else if ((**definition_existing).get()->kind == DefinitionKind::ExplicitTable) {
                return Some(error(TomlErrorCode::DuplicateTable));
            } else {
                return Some(error(TomlErrorCode::TableRedefinition));
            }
        }
        current_path_ = rstd::move(owned_path);
        return finish_line();
    }

public:
    explicit TomlParser(ref<str> input, ParseOptions options) noexcept
        : cursor_(rstd::parse::text_input(input)),
          remaining_depth_(options.max_depth),
          max_input_bytes_(options.max_input_bytes),
          remaining_values_(options.max_values) {}

    [[nodiscard]]
    auto parse_standalone_key() -> Result<KeyPath, Error> {
        if (cursor_.len() > max_input_bytes_) {
            return Err(error(TomlErrorCode::ResourceLimitExceeded));
        }
        consume_horizontal();
        auto key = parse_key_path();
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        consume_horizontal();
        if (! eof()) return Err(error(TomlErrorCode::ExpectedLineEnd));
        return key;
    }

    [[nodiscard]]
    auto parse_standalone_value() -> ParseResult {
        if (remaining_depth_ == u8()) {
            return Err(error(TomlErrorCode::RecursionLimitExceeded));
        }
        if (cursor_.len() > max_input_bytes_) {
            return Err(error(TomlErrorCode::ResourceLimitExceeded));
        }
        consume_horizontal();
        auto value = parse_value();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        consume_horizontal();
        if (! eof()) return Err(error(TomlErrorCode::ExpectedLineEnd));
        return value;
    }

    [[nodiscard]]
    auto parse_standalone_assignment() -> Result<Assignment, Error> {
        if (remaining_depth_ == u8()) {
            return Err(error(TomlErrorCode::RecursionLimitExceeded));
        }
        if (cursor_.len() > max_input_bytes_) {
            return Err(error(TomlErrorCode::ResourceLimitExceeded));
        }
        consume_horizontal();
        auto key = parse_key_path();
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        consume_horizontal();
        if (eof() || peek() != u8('=')) return Err(error(TomlErrorCode::ExpectedEquals));
        take();
        consume_horizontal();
        auto value = parse_value();
        if (value.is_err()) return Err(rstd::move(value).unwrap_err());
        consume_horizontal();
        if (! eof()) return Err(error(TomlErrorCode::ExpectedLineEnd));
        return Ok(
            Assignment { .key = rstd::move(key).unwrap(), .value = rstd::move(value).unwrap() });
    }

    [[nodiscard]]
    auto parse_standalone_assignment_text() -> Result<AssignmentText, Error> {
        if (cursor_.len() > max_input_bytes_) {
            return Err(error(TomlErrorCode::ResourceLimitExceeded));
        }
        consume_horizontal();
        auto key = parse_key_path();
        if (key.is_err()) return Err(rstd::move(key).unwrap_err());
        consume_horizontal();
        if (eof() || peek() != u8('=')) return Err(error(TomlErrorCode::ExpectedEquals));
        take();
        consume_horizontal();
        auto span = rstd::parse::Span { .begin = cursor_.position(), .end = cursor_.len() };
        while (! span.is_empty() && is_horizontal(cursor_.input()[span.end - usize(1)])) --span.end;
        if (span.is_empty()) return Err(error(TomlErrorCode::ExpectedValue));
        return Ok(AssignmentText {
            .key   = rstd::move(key).unwrap(),
            .value = String::make(cursor_.text(span)),
        });
    }

    [[nodiscard]]
    auto parse() -> ParseResult {
        if (remaining_depth_ == u8()) {
            return Err(error(TomlErrorCode::RecursionLimitExceeded));
        }
        if (cursor_.len() > max_input_bytes_) {
            return Err(error(TomlErrorCode::ResourceLimitExceeded));
        }
        if (auto failure = consume_document_trivia(); failure.is_some()) return Err(*failure);
        while (! eof()) {
            Option<Error> failure = peek() == u8('[') ? parse_header() : parse_assignment();
            if (failure.is_some()) return Err(*failure);
            if (auto trivia = consume_document_trivia(); trivia.is_some()) return Err(*trivia);
        }
        return Ok(Value::Table(rstd::move(root_)));
    }
};

namespace rstd::toml
{

auto from_str(ref<str> input) -> ParseResult {
    return TomlParser(input, {}).parse();
}

auto from_str(ref<str> input, ParseOptions options) -> ParseResult {
    return TomlParser(input, options).parse();
}

auto from_slice(slice<u8> input) -> ParseResult {
    return from_slice(input, {});
}

auto from_slice(slice<u8> input, ParseOptions options) -> ParseResult {
    auto text = str_::from_utf8(input);
    if (text.is_err()) return Err(TomlParser::invalid_utf8_error());
    return from_str(rstd::move(text).unwrap_unchecked(), options);
}

auto parse_key_path(ref<str> input) -> rstd::Result<KeyPath, Error> {
    return TomlParser(input, {}).parse_standalone_key();
}

auto parse_value(ref<str> input) -> ParseResult {
    return TomlParser(input, {}).parse_standalone_value();
}

auto parse_assignment(ref<str> input) -> rstd::Result<Assignment, Error> {
    return TomlParser(input, {}).parse_standalone_assignment();
}

auto parse_assignment_text(ref<str> input) -> rstd::Result<AssignmentText, Error> {
    return TomlParser(input, {}).parse_standalone_assignment_text();
}

} // namespace rstd::toml

namespace rstd
{

template<>
struct Impl<str_::FromStr, toml::Value> : ImplBase<toml::Value> {
    using Err = toml::Error;
    static auto from_str(ref<str> input) -> toml::ParseResult { return toml::from_str(input); }
};

} // namespace rstd

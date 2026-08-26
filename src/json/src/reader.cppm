export module rstd.json:reader;
import :number;
import :error;
import :parser;
import rstd.parse.core;

using namespace rstd::prelude;
using ::alloc::string::String;

enum class JsonValueKind : rstd::uint8_t
{
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

class JsonReader {
    rstd::parse::TextCursor  cursor_;
    u8                       remaining_depth_ { 128 };
    rstd::json::ParseOptions options_;

    auto eof() const noexcept -> bool;
    auto peek() const noexcept -> u8;
    auto peek_next() const noexcept -> u8;
    auto take() noexcept -> u8;
    auto consume_whitespace() noexcept -> Option<rstd::json::Error>;
    auto error(ErrorCode code) const noexcept -> rstd::json::Error;
    auto error_after_consumed(ErrorCode code) const noexcept -> rstd::json::Error;
    auto consume_ident(ref<str> value) -> Result<empty, rstd::json::Error>;
    auto parse_hex_escape() -> Result<u16, rstd::json::Error>;
    auto parse_unicode_escape(String& output) -> Result<empty, rstd::json::Error>;
    auto
    parse_float(rstd::parse::Span integer, rstd::parse::Span fraction, i32 exponent, bool negative)
        -> Result<rstd::json::Number, rstd::json::Error>;
    auto leave_container(u8 delimiter) noexcept -> void;

public:
    explicit JsonReader(ref<str> input, rstd::json::ParseOptions options = {}) noexcept;

    auto value_kind() -> Result<JsonValueKind, rstd::json::Error>;
    auto parse_null() -> Result<empty, rstd::json::Error>;
    auto parse_bool() -> Result<bool, rstd::json::Error>;
    auto parse_string() -> Result<String, rstd::json::Error>;
    auto parse_number() -> Result<rstd::json::Number, rstd::json::Error>;
    auto begin_array() -> Result<empty, rstd::json::Error>;
    auto next_array(bool& first) -> Result<bool, rstd::json::Error>;
    auto begin_object() -> Result<empty, rstd::json::Error>;
    auto next_object_key(bool& first) -> Result<Option<String>, rstd::json::Error>;
    auto skip_value() -> Result<empty, rstd::json::Error>;
    auto finish() -> Result<empty, rstd::json::Error>;
    auto duplicate_key_error() const noexcept -> rstd::json::Error {
        return error(ErrorCode::DuplicateObjectKey);
    }
    auto reject_duplicate_keys() const noexcept -> bool { return options_.reject_duplicate_keys; }
};

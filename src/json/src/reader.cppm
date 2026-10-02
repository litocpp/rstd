export module rstd.json:reader;
export import :number;
export import :error;
export import :parser;
import rstd.parse.core;

using namespace rstd::prelude;
using ::alloc::string::String;
using ::alloc::vec::Vec;

export namespace rstd::json
{

enum class ValueKind : rstd::uint8_t
{
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

enum class FloatOverflow : rstd::uint8_t
{
    Reject,
    Infinity
};

/// Reads borrowed UTF-8 text or UTF-16 code units without imposing a DOM representation.
class Reader {
    rstd::parse::TextCursor  cursor_;
    rstd::parse::Cursor<u16> units_;
    bool                     utf16_;
    usize                    remaining_depth_;
    ParseOptions             options_;

    struct NumberToken {
        rstd::parse::Span integer;
        rstd::parse::Span fraction;
        i32               exponent {};
        bool              negative {};
        bool              floating {};
    };

    auto eof() const noexcept -> bool;
    auto peek(usize ahead = usize()) const noexcept -> u16;
    auto take() noexcept -> u16;
    auto unit_at(usize offset) const noexcept -> u16;
    auto consume_whitespace() noexcept -> Option<Error>;
    auto error(ErrorCode code) const noexcept -> Error;
    auto error_after_consumed(ErrorCode code) const noexcept -> Error;
    auto consume_ident(ref<str> value) -> Result<empty, Error>;
    auto parse_hex_escape() -> Result<u16, Error>;
    template<typename Output>
    auto parse_unicode_escape(Output& output) -> Result<empty, Error>;
    auto append_utf16(String& output, slice<u16> units) -> Result<empty, Error>;
    template<typename Output>
    auto append_raw(Output& output, rstd::parse::Span span) -> Result<empty, Error>;
    template<typename Output>
    auto read_string() -> Result<Output, Error>;
    auto scan_number() -> Result<NumberToken, Error>;
    auto parse_float(const NumberToken& token, FloatOverflow overflow) -> Result<f64, Error>;
    auto leave_container() noexcept -> void;
    template<typename Output>
    auto read_object_key(bool& first) -> Result<Option<Output>, Error>;

public:
    explicit Reader(ref<str> input, ParseOptions options = {}) noexcept;
    explicit Reader(slice<u16> input, ParseOptions options = {}) noexcept;

    /// Returns the consumed byte or UTF-16 code-unit count.
    auto position() const noexcept -> usize;
    auto value_kind() -> Result<ValueKind, Error>;
    auto parse_null() -> Result<empty, Error>;
    auto parse_bool() -> Result<bool, Error>;
    /// Decodes a Unicode scalar string, rejecting unpaired surrogates.
    auto parse_string() -> Result<String, Error>;
    /// Decodes code units, preserving unpaired surrogates.
    auto parse_string_units() -> Result<Vec<u16>, Error>;
    auto parse_number() -> Result<Number, Error>;
    auto parse_f64(FloatOverflow overflow = FloatOverflow::Reject) -> Result<f64, Error>;
    auto begin_array() -> Result<empty, Error>;
    auto next_array(bool& first) -> Result<bool, Error>;
    auto begin_object() -> Result<empty, Error>;
    auto next_object_key(bool& first) -> Result<Option<String>, Error>;
    auto next_object_key_units(bool& first) -> Result<Option<Vec<u16>>, Error>;
    auto skip_value() -> Result<empty, Error>;
    auto finish() -> Result<empty, Error>;
    auto duplicate_key_error() const noexcept -> Error {
        return error(ErrorCode::DuplicateObjectKey);
    }
    auto reject_duplicate_keys() const noexcept -> bool { return options_.reject_duplicate_keys; }
};

} // namespace rstd::json

using JsonReader    = rstd::json::Reader;
using JsonValueKind = rstd::json::ValueKind;

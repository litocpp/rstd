export module rstd.toml:parser;
export import :value;
export import :error;

using ::alloc::string::String;
using ::alloc::vec::Vec;

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

namespace rstd
{

template<>
struct Impl<str_::FromStr, toml::Value> : ImplBase<toml::Value> {
    using Err = toml::Error;
    static auto from_str(ref<str> input) -> toml::ParseResult { return toml::from_str(input); }
};

} // namespace rstd

export module rstd.json:parser;
export import :value;
export import :error;

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

namespace rstd
{

template<>
struct Impl<str_::FromStr, json::Value> : ImplBase<json::Value> {
    using Err = json::Error;
    static auto from_str(ref<str> input) -> json::ParseResult { return json::from_str(input); }
};

} // namespace rstd

export module rstd.json:string;
export import rstd.alloc;

using namespace rstd::prelude;
using ::alloc::string::String;
using ::alloc::vec::Vec;

namespace rstd::json
{

template<typename T>
auto quote_units(slice<T> input) -> Vec<T> {
    Vec<T> output;
    output.push(T('"'));
    constexpr const char* hex = "0123456789abcdef";
    for (usize index {}; index < input.len(); ++index) {
        const auto value = input[index];
        char       escape {};
        switch (value.to_primitive()) {
        case '"': escape = '"'; break;
        case '\\': escape = '\\'; break;
        case '\b': escape = 'b'; break;
        case '\f': escape = 'f'; break;
        case '\n': escape = 'n'; break;
        case '\r': escape = 'r'; break;
        case '\t': escape = 't'; break;
        }
        if (escape != 0) {
            output.push(T('\\'));
            output.push(T(escape));
            continue;
        }
        bool surrogate = false;
        if constexpr (rstd::mtp::same_as<T, u16>) {
            if (value >= u16(0xd800) && value <= u16(0xdbff) && index + usize(1) < input.len() &&
                input[index + usize(1)] >= u16(0xdc00) && input[index + usize(1)] <= u16(0xdfff)) {
                output.push(T(value));
                output.push(T(input[++index]));
                continue;
            }
            surrogate = value >= u16(0xd800) && value <= u16(0xdfff);
        }
        if (value < T(0x20) || surrogate) {
            const auto cp = value.to_primitive();
            output.push(T('\\'));
            output.push(T('u'));
            output.push(T(hex[(cp >> 12) & 15]));
            output.push(T(hex[(cp >> 8) & 15]));
            output.push(T(hex[(cp >> 4) & 15]));
            output.push(T(hex[cp & 15]));
        } else
            output.push(T(value));
    }
    output.push(T('"'));
    return output;
}

} // namespace rstd::json

export namespace rstd::json
{

/// Quotes valid UTF-8 text as a JSON string.
inline auto quote_string(ref<str> input) -> String {
    return String::from_utf8_unchecked(quote_units(input.as_bytes()));
}

/// Quotes UTF-16 code units, escaping unpaired surrogates.
inline auto quote_string(slice<u16> input) -> Vec<u16> {
    return quote_units(input);
}

} // namespace rstd::json

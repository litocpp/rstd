export module rstd.json:string;
export import rstd.alloc;

using namespace rstd::prelude;
using ::alloc::string::String;
using ::alloc::vec::Vec;

namespace rstd::json
{

template<typename T, typename Writer>
auto write_quoted_units(slice<T> input, Writer write) -> bool {
    const rstd::array<T, 1> quote { T('"') };
    if (! write(quote.as_slice())) return false;
    constexpr const char* hex = "0123456789abcdef";
    usize                 begin {};
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
        bool surrogate = false;
        if constexpr (rstd::mtp::same_as<T, u16>) {
            if (value >= u16(0xd800) && value <= u16(0xdbff) && index + usize(1) < input.len() &&
                input[index + usize(1)] >= u16(0xdc00) && input[index + usize(1)] <= u16(0xdfff)) {
                ++index;
                continue;
            }
            surrogate = value >= u16(0xd800) && value <= u16(0xdfff);
        }
        if (escape == 0 && value >= T(0x20) && ! surrogate) continue;
        if (index > begin && ! write(slice<T>::from_raw_parts(
                                 input.as_raw_ptr() + begin.to_primitive(), index - begin)))
            return false;
        rstd::array<T, 6> encoded {};
        encoded[usize()] = T('\\');
        usize length(2);
        if (escape != 0) {
            encoded[usize(1)] = T(escape);
        } else {
            const auto cp     = value.to_primitive();
            encoded[usize(1)] = T('u');
            encoded[usize(2)] = T(hex[(cp >> 12) & 15]);
            encoded[usize(3)] = T(hex[(cp >> 8) & 15]);
            encoded[usize(4)] = T(hex[(cp >> 4) & 15]);
            encoded[usize(5)] = T(hex[cp & 15]);
            length            = usize(6);
        }
        if (! write(slice<T>::from_raw_parts(encoded.as_slice().as_raw_ptr(), length)))
            return false;
        begin = index + usize(1);
    }
    if (begin < input.len() && ! write(slice<T>::from_raw_parts(
                                   input.as_raw_ptr() + begin.to_primitive(), input.len() - begin)))
        return false;
    return write(quote.as_slice());
}

template<typename T>
auto quote_units(slice<T> input) -> Vec<T> {
    Vec<T> output;
    write_quoted_units(input, [&](slice<T> units) -> bool {
        output.extend_from_slice(units);
        return true;
    });
    return output;
}

} // namespace rstd::json

export namespace rstd::json
{

/// Writes quoted UTF-8 text, stopping immediately if the formatter rejects output.
inline auto write_quoted_string(fmt::Formatter& formatter, ref<str> input) -> bool {
    return write_quoted_units(input.as_bytes(), [&](slice<u8> bytes) -> bool {
        return formatter.write_raw(bytes.as_raw_ptr(), bytes.len().to_primitive());
    });
}

/// Quotes valid UTF-8 text as a JSON string.
inline auto quote_string(ref<str> input) -> String {
    return String::from_utf8_unchecked(quote_units(input.as_bytes()));
}

/// Quotes UTF-16 code units, escaping unpaired surrogates.
inline auto quote_string(slice<u16> input) -> Vec<u16> {
    return quote_units(input);
}

} // namespace rstd::json

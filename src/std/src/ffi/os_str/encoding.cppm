export module rstd:ffi.os_str.encoding;
import rstd.alloc;

using namespace rstd::prelude;
using ::alloc::vec::Vec;
using ::alloc::string::String;

namespace rstd::ffi::os_encoding
{

constexpr auto unit(slice<u8> bytes, usize index) -> unsigned {
    return bytes[index].to_primitive();
}

// Input must be valid WTF-8, including surrogate code points.
constexpr auto decode(slice<u8> bytes, usize& index) -> unsigned {
    auto first = unit(bytes, index++);
    if (first < 0x80) return first;
    auto count = first < 0xe0 ? 1u : first < 0xf0 ? 2u : 3u;
    auto value = first & (count == 1 ? 0x1fu : count == 2 ? 0x0fu : 0x07u);
    while (count-- != 0) value = (value << 6) | (unit(bytes, index++) & 0x3fu);
    return value;
}

constexpr auto decode_lossy(slice<u8> bytes, usize& index, bool wtf8) -> char32_t {
    if (wtf8) {
        auto value = decode(bytes, index);
        return value >= 0xd800 && value <= 0xdfff ? char_::REPLACEMENT
                                                  : static_cast<char32_t>(value);
    }
    auto remaining =
        slice<u8>::from_raw_parts(bytes.as_raw_ptr() + index.to_primitive(), bytes.len() - index);
    auto [value, length] = char_::decode_utf8(remaining.as_raw_ptr(), remaining.len());
    if (value == char_::REPLACEMENT && length == usize(1) && unit(remaining, usize()) >= 0x80) {
        auto prefix = slice<u8>::from_raw_parts(
            remaining.as_raw_ptr(), remaining.len() < usize(4) ? remaining.len() : usize(4));
        auto error   = str_::validate_utf8(prefix).unwrap_err_unchecked();
        auto invalid = error.error_len();
        length       = invalid.is_some() ? usize(invalid->to_primitive()) : prefix.len();
    }
    index += length;
    return value;
}

void encode(Vec<u8>& bytes, unsigned value) {
    if (value < 0x80)
        bytes.push(u8(value));
    else {
        if (value < 0x800)
            bytes.push(u8(0xc0 | (value >> 6)));
        else {
            if (value < 0x10000)
                bytes.push(u8(0xe0 | (value >> 12)));
            else {
                bytes.push(u8(0xf0 | (value >> 18)));
                bytes.push(u8(0x80 | ((value >> 12) & 0x3f)));
            }
            bytes.push(u8(0x80 | ((value >> 6) & 0x3f)));
        }
        bytes.push(u8(0x80 | (value & 0x3f)));
    }
}

auto from_wide(slice<u16> wide) -> Vec<u8> {
    auto result = Vec<u8>::with_capacity(wide.len());
    for (auto index = usize(); index < wide.len(); ++index) {
        unsigned value = wide[index].to_primitive();
        if (value >= 0xd800 && value <= 0xdbff && index + usize(1) < wide.len()) {
            auto next = wide[index + usize(1)].to_primitive();
            if (next >= 0xdc00 && next <= 0xdfff) {
                value = 0x10000 + ((value - 0xd800) << 10) + next - 0xdc00;
                ++index;
            }
        }
        encode(result, value);
    }
    return result;
}

void append_wtf8(Vec<u8>& output, slice<u8> input) {
    auto offset = usize();
    if (output.len() >= usize(3) && input.len() >= usize(3)) {
        auto start = output.len() - usize(3);
        auto left  = output.as_slice();
        if (unit(left, start) == 0xed && unit(left, start + usize(1)) >= 0xa0 &&
            unit(left, start + usize(1)) <= 0xaf && unit(input, usize()) == 0xed &&
            unit(input, usize(1)) >= 0xb0 && unit(input, usize(1)) <= 0xbf) {
            auto lead  = decode(left, start);
            auto trail = decode(input, offset);
            output.truncate(output.len() - usize(3));
            encode(output, 0x10000 + ((lead - 0xd800) << 10) + trail - 0xdc00);
        }
    }
    for (; offset < input.len(); ++offset) output.push(input[offset]);
}

constexpr auto public_boundary(slice<u8> bytes, usize index) -> bool {
    if (index == usize() || index == bytes.len()) return true;
    if (index > bytes.len()) return false;
    if (unit(bytes, index - usize(1)) < 0x80 || unit(bytes, index) < 0x80) return true;
    auto remaining = bytes.len() - index;
    auto after     = slice<u8>::from_raw_parts(bytes.as_raw_ptr() + index.to_primitive(),
                                               remaining < usize(4) ? remaining : usize(4));
    auto decoded   = char_::decode_utf8(after.as_raw_ptr(), after.len());
    if (decoded.template get<1>() > usize(1)) return true;
    for (auto length = usize(2); length <= usize(4) && length <= index; ++length) {
        auto before =
            slice<u8>::from_raw_parts(bytes.as_raw_ptr() + (index - length).to_primitive(), length);
        if (str_::validate_utf8(before).is_ok()) return true;
    }
    return false;
}

} // namespace rstd::ffi::os_encoding

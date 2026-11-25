export module rstd.core:char_.utf8;
import :num.types;
export import rstd.basic;

namespace rstd::char_
{
constexpr auto is_scalar(char32_t value) noexcept -> bool {
    return value <= 0x10FFFF && (value < 0xD800 || value > 0xDFFF);
}
constexpr auto len_utf8_scalar(char32_t c) noexcept -> usize {
    return usize(c <= 0x7F ? 1 : c <= 0x7FF ? 2 : c <= 0xFFFF ? 3 : 4);
}

constexpr auto encode_utf8_scalar(char32_t c, byte* buf) noexcept -> usize {
    auto write = [buf](rstd::size_t index, auto value) constexpr {
        [[unsafe]] { buf[index] = byte(static_cast<rstd::uint8_t>(value)); }
    };
    if (c <= 0x7F) {
        write(0, c);
        return usize(1);
    }
    if (c <= 0x7FF) {
        write(0, 0xC0 | (c >> 6));
        write(1, 0x80 | (c & 0x3F));
        return usize(2);
    }
    if (c <= 0xFFFF) {
        write(0, 0xE0 | (c >> 12));
        write(1, 0x80 | ((c >> 6) & 0x3F));
        write(2, 0x80 | (c & 0x3F));
        return usize(3);
    }
    write(0, 0xF0 | (c >> 18));
    write(1, 0x80 | ((c >> 12) & 0x3F));
    write(2, 0x80 | ((c >> 6) & 0x3F));
    write(3, 0x80 | (c & 0x3F));
    return usize(4);
}
} // namespace rstd::char_

export namespace rstd::char_
{
/// The highest valid Unicode code point (U+10FFFF).
inline constexpr char32_t MAX = 0x10FFFF;

/// The Unicode replacement character (U+FFFD), used for invalid sequences.
inline constexpr char32_t REPLACEMENT = 0xFFFD;

/// Returns the number of bytes needed to encode this code point in UTF-8 (1–4).
///
/// Returns 0 for invalid code points (surrogates, > U+10FFFF).
constexpr auto len_utf8(char32_t c) noexcept -> usize {
    if (! is_scalar(c)) return usize();
    return len_utf8_scalar(c);
}

/// Encodes a Unicode code point into UTF-8.
///
/// \param c    The code point to encode.
/// \param buf  Output buffer (must have room for at least `len_utf8(c)` bytes).
/// \return Number of bytes written (1–4), or 0 on invalid code point.
[[unsafe]]
constexpr auto encode_utf8(char32_t c, byte* buf) noexcept -> usize {
    if (! is_scalar(c)) return usize();
    return encode_utf8_scalar(c, buf);
}

/// Decodes one UTF-8 code point from the start of a byte sequence.
///
/// \param ptr  Pointer to a live, readable range of at least `len` bytes.
/// \param len  Number of available bytes.
/// \return `{code_point, bytes_consumed}`.
///         On invalid/incomplete sequence, returns `{REPLACEMENT, 1}` (skip one byte).
[[unsafe]]
constexpr auto decode_utf8(const byte* ptr, usize len) noexcept -> rstd::tuple<char32_t, usize> {
    auto const available = len.to_primitive();
    if (available == 0) return { REPLACEMENT, usize() };

    auto read = [ptr](rstd::size_t index) constexpr {
        [[unsafe]] { return u8::from_byte(ptr[index]).to_primitive(); }
    };
    auto const b0 = read(0);

    // 1-byte (ASCII)
    if (b0 <= 0x7F) {
        return { char32_t(b0), usize(1) };
    }

    // Determine expected sequence length from leading byte.
    rstd::size_t seq_len;
    char32_t     cp;
    if ((b0 & 0xE0) == 0xC0) {
        seq_len = 2;
        cp      = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        seq_len = 3;
        cp      = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        seq_len = 4;
        cp      = b0 & 0x07;
    } else {
        // Invalid leading byte (continuation or 0xFE/0xFF).
        return { REPLACEMENT, usize(1) };
    }

    if (seq_len > available) return { REPLACEMENT, usize(1) };

    // Consume continuation bytes.
    for (rstd::size_t i = 1; i < seq_len; ++i) {
        auto const b = read(i);
        if ((b & 0xC0) != 0x80) return { REPLACEMENT, usize(1) };
        cp = (cp << 6) | (b & 0x3F);
    }

    // Reject overlong encodings.
    if (seq_len == 2 && cp < 0x80) return { REPLACEMENT, usize(1) };
    if (seq_len == 3 && cp < 0x800) return { REPLACEMENT, usize(1) };
    if (seq_len == 4 && cp < 0x10000) return { REPLACEMENT, usize(1) };

    // Reject surrogates and out-of-range.
    if (! is_scalar(cp)) return { REPLACEMENT, usize(1) };

    return { cp, usize(seq_len) };
}

/// Returns `true` if the byte is a UTF-8 continuation byte (10xxxxxx).
constexpr auto is_continuation(u8 value) noexcept -> bool {
    return (value.to_primitive() & 0xC0) == 0x80;
}

/// Returns `true` if position `pos` is on a UTF-8 character boundary
/// within the byte sequence `[ptr, ptr+len)`.
///
/// Positions 0 and `len` are always boundaries.
/// The byte range must be live and readable.
[[unsafe]]
constexpr auto is_char_boundary(const byte* ptr, usize len, usize pos) noexcept -> bool {
    if (pos == usize() || pos == len) return true;
    if (pos > len) return false;
    [[unsafe]] { return ! is_continuation(u8::from_byte(ptr[pos.to_primitive()])); }
}

/// Validates whether a live, readable byte range is well-formed UTF-8.
///
/// \return `true` if all bytes form valid UTF-8.
[[unsafe]]
constexpr auto is_valid_utf8(const byte* ptr, usize len) noexcept -> bool {
    rstd::size_t i         = 0;
    auto const   available = len.to_primitive();
    while (i < available) {
        [[unsafe]] {
            auto [cp, n]        = decode_utf8(ptr + i, usize(available - i));
            auto const consumed = n.to_primitive();
            if (cp == REPLACEMENT && consumed <= 1 && u8::from_byte(ptr[i]).to_primitive() > 0x7F)
                return false;
            i += consumed;
        }
    }
    return true;
}

} // namespace rstd::char_

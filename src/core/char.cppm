export module rstd.core:char_;
export import rstd.basic;

export namespace rstd::char_
{

/// The highest valid Unicode code point (U+10FFFF).
inline constexpr char32_t MAX = 0x10FFFF;

/// The Unicode replacement character (U+FFFD), used for invalid sequences.
inline constexpr char32_t REPLACEMENT = 0xFFFD;

/// Returns `true` if the code point is in the ASCII range (0x00–0x7F).
constexpr auto is_ascii(char32_t c) noexcept -> bool {
    return c <= 0x7F;
}

/// Returns the number of bytes needed to encode this code point in UTF-8 (1–4).
///
/// Returns 0 for invalid code points (surrogates, > U+10FFFF).
constexpr auto len_utf8(char32_t c) noexcept -> usize {
    if (c <= 0x7F)       return 1;
    if (c <= 0x7FF)      return 2;
    if (c <= 0xFFFF) {
        // Surrogates are not valid scalar values.
        if (c >= 0xD800 && c <= 0xDFFF) return 0;
        return 3;
    }
    if (c <= 0x10FFFF)   return 4;
    return 0;
}

/// Encodes a Unicode code point into UTF-8.
///
/// \param c    The code point to encode.
/// \param buf  Output buffer (must have room for at least `len_utf8(c)` bytes).
/// \return Number of bytes written (1–4), or 0 on invalid code point.
constexpr auto encode_utf8(char32_t c, u8* buf) noexcept -> usize {
    if (c <= 0x7F) {
        buf[0] = static_cast<u8>(c);
        return 1;
    }
    if (c <= 0x7FF) {
        buf[0] = static_cast<u8>(0xC0 | (c >> 6));
        buf[1] = static_cast<u8>(0x80 | (c & 0x3F));
        return 2;
    }
    if (c <= 0xFFFF) {
        if (c >= 0xD800 && c <= 0xDFFF) return 0;
        buf[0] = static_cast<u8>(0xE0 | (c >> 12));
        buf[1] = static_cast<u8>(0x80 | ((c >> 6) & 0x3F));
        buf[2] = static_cast<u8>(0x80 | (c & 0x3F));
        return 3;
    }
    if (c <= 0x10FFFF) {
        buf[0] = static_cast<u8>(0xF0 | (c >> 18));
        buf[1] = static_cast<u8>(0x80 | ((c >> 12) & 0x3F));
        buf[2] = static_cast<u8>(0x80 | ((c >> 6) & 0x3F));
        buf[3] = static_cast<u8>(0x80 | (c & 0x3F));
        return 4;
    }
    return 0;
}

/// Decodes one UTF-8 code point from the start of a byte sequence.
///
/// \param ptr  Pointer to the first byte.
/// \param len  Number of available bytes.
/// \return `{code_point, bytes_consumed}`.
///         On invalid/incomplete sequence, returns `{REPLACEMENT, 1}` (skip one byte).
constexpr auto decode_utf8(const u8* ptr, usize len) noexcept
    -> rstd::tuple<char32_t, usize> {
    if (len == 0) return { REPLACEMENT, 0 };

    u8 b0 = ptr[0];

    // 1-byte (ASCII)
    if (b0 <= 0x7F) {
        return { static_cast<char32_t>(b0), 1 };
    }

    // Determine expected sequence length from leading byte.
    usize seq_len;
    char32_t cp;
    if ((b0 & 0xE0) == 0xC0) {
        seq_len = 2;
        cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        seq_len = 3;
        cp = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        seq_len = 4;
        cp = b0 & 0x07;
    } else {
        // Invalid leading byte (continuation or 0xFE/0xFF).
        return { REPLACEMENT, 1 };
    }

    if (seq_len > len) return { REPLACEMENT, 1 };

    // Consume continuation bytes.
    for (usize i = 1; i < seq_len; i++) {
        u8 b = ptr[i];
        if ((b & 0xC0) != 0x80) return { REPLACEMENT, 1 };
        cp = (cp << 6) | (b & 0x3F);
    }

    // Reject overlong encodings.
    if (seq_len == 2 && cp < 0x80)    return { REPLACEMENT, 1 };
    if (seq_len == 3 && cp < 0x800)   return { REPLACEMENT, 1 };
    if (seq_len == 4 && cp < 0x10000) return { REPLACEMENT, 1 };

    // Reject surrogates and out-of-range.
    if (cp >= 0xD800 && cp <= 0xDFFF) return { REPLACEMENT, 1 };
    if (cp > MAX)                     return { REPLACEMENT, 1 };

    return { cp, seq_len };
}

/// Returns `true` if the byte is a UTF-8 continuation byte (10xxxxxx).
constexpr auto is_continuation(u8 b) noexcept -> bool {
    return (b & 0xC0) == 0x80;
}

/// Returns `true` if position `pos` is on a UTF-8 character boundary
/// within the byte sequence `[ptr, ptr+len)`.
///
/// Positions 0 and `len` are always boundaries.
constexpr auto is_char_boundary(const u8* ptr, usize len, usize pos) noexcept -> bool {
    if (pos == 0 || pos == len) return true;
    if (pos > len) return false;
    return ! is_continuation(ptr[pos]);
}

/// Validates that a byte sequence is well-formed UTF-8.
///
/// \return `true` if all bytes form valid UTF-8.
constexpr auto is_valid_utf8(const u8* ptr, usize len) noexcept -> bool {
    usize i = 0;
    while (i < len) {
        auto [cp, n] = decode_utf8(ptr + i, len - i);
        if (cp == REPLACEMENT && (n <= 1 && (i >= len || ptr[i] > 0x7F)))
            return false;
        i += n;
    }
    return true;
}

} // namespace rstd::char_

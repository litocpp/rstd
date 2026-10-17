export module rstd.core:char_.types;
import :num.types;
import :unicode;
import :option;
import :clone;
import :hash;
import :fmt;
import :str.str;
export import :char_.utf8;

export namespace rstd::char_
{
inline constexpr auto& UNICODE_VERSION = unicode::version;
}

export namespace rstd::char_
{
/// Returns whether the scalar value has the Unicode Alphabetic property.
constexpr auto is_alphabetic(char32_t c) noexcept -> bool {
    if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z')) return true;
    if (c <= 0xA9) return false;
    return unicode::alphabetic(c);
}

/// Returns whether the scalar value belongs to Unicode category Lu, Ll, Lt, Lm, or Lo.
constexpr auto is_letter(char32_t c) noexcept -> bool {
    if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z')) return true;
    if (c <= 0xA9) return false;
    return unicode::letter(c);
}

/// Returns whether the scalar value belongs to Unicode category Nd, Nl, or No.
constexpr auto is_numeric(char32_t c) noexcept -> bool {
    if (c >= U'0' && c <= U'9') return true;
    if (c <= 0xB1) return false;
    return unicode::numeric(c);
}

constexpr auto is_alphanumeric(char32_t c) noexcept -> bool {
    return is_alphabetic(c) || is_numeric(c);
}

/// Returns whether the scalar value has the Unicode White_Space property.
constexpr auto is_whitespace(char32_t c) noexcept -> bool {
    if (c == U' ' || (c >= 0x09 && c <= 0x0D)) return true;
    if (c <= 0x84) return false;
    return unicode::whitespace(c);
}

/// Returns `true` if the code point is in the ASCII range (0x00–0x7F).
constexpr auto is_ascii(char32_t c) noexcept -> bool {
    return c <= 0x7F;
}

} // namespace rstd::char_

export namespace rstd
{
class Char {
    uint32_t value_ {};
    constexpr explicit Char(uint32_t value) noexcept: value_(value) {}

public:
    constexpr Char() noexcept = default;
    static constexpr auto from_u32(u32 value) noexcept -> option::Option<Char>;
    static constexpr auto from_char32(char32_t value) noexcept -> option::Option<Char>;

    /// The caller must supply a Unicode scalar value, excluding surrogates.
    static constexpr auto from_u32_unchecked(u32 value) noexcept -> Char {
        return Char(value.to_primitive());
    }

    constexpr auto        to_u32() const noexcept -> u32 { return u32(value_); }
    constexpr auto        to_primitive() const noexcept -> char32_t { return char32_t(value_); }
    friend constexpr auto operator==(Char, Char) noexcept -> bool = default;
    friend constexpr auto operator<=>(Char a, Char b) noexcept { return a.value_ <=> b.value_; }

    static const Char      MIN;
    static const Char      MAX;
    static const Char      REPLACEMENT_CHARACTER;
    static constexpr auto& UNICODE_VERSION = unicode::version;

    constexpr auto is_ascii() const noexcept -> bool;
    constexpr auto is_alphabetic() const noexcept -> bool;
    constexpr auto is_letter() const noexcept -> bool;
    constexpr auto is_numeric() const noexcept -> bool;
    constexpr auto is_alphanumeric() const noexcept -> bool;
    constexpr auto is_whitespace() const noexcept -> bool;
    constexpr auto len_utf8() const noexcept -> usize;
    constexpr auto encode_utf8(byte (&buffer)[4]) const noexcept -> usize;
};

inline constexpr Char Char::MIN { 0 };
inline constexpr Char Char::MAX { char_::MAX };
inline constexpr Char Char::REPLACEMENT_CHARACTER { char_::REPLACEMENT };
static_assert(sizeof(Char) == 4);
} // namespace rstd

namespace rstd
{
constexpr auto Char::is_ascii() const noexcept -> bool {
    return char_::is_ascii(to_primitive());
}
constexpr auto Char::is_alphabetic() const noexcept -> bool {
    return char_::is_alphabetic(to_primitive());
}
constexpr auto Char::is_numeric() const noexcept -> bool {
    return char_::is_numeric(to_primitive());
}
constexpr auto Char::is_letter() const noexcept -> bool {
    return char_::is_letter(to_primitive());
}
constexpr auto Char::is_alphanumeric() const noexcept -> bool {
    return char_::is_alphanumeric(to_primitive());
}
constexpr auto Char::is_whitespace() const noexcept -> bool {
    return char_::is_whitespace(to_primitive());
}
} // namespace rstd

namespace rstd
{
constexpr auto Char::from_u32(u32 value) noexcept -> option::Option<Char> {
    return from_char32(char32_t(value.to_primitive()));
}

constexpr auto Char::from_char32(char32_t value) noexcept -> option::Option<Char> {
    if (! char_::is_scalar(value)) return None();
    return Some(Char(uint32_t(value)));
}
} // namespace rstd

namespace rstd
{
constexpr auto Char::len_utf8() const noexcept -> usize {
    return char_::len_utf8_scalar(to_primitive());
}
constexpr auto Char::encode_utf8(byte (&buffer)[4]) const noexcept -> usize {
    return char_::encode_utf8_scalar(to_primitive(), buffer);
}
} // namespace rstd

export namespace rstd::char_
{
constexpr auto decode_char_utf8(const byte* ptr, usize len) noexcept -> rstd::tuple<Char, usize> {
    auto [value, consumed] = decode_utf8(ptr, len);
    return { Char::from_u32_unchecked(u32(value)), consumed };
}
} // namespace rstd::char_

namespace rstd
{
template<>
struct Impl<hash::Hash, Char> : ImplBase<Char> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::write_value(state, this->self().to_u32());
    }
};

template<>
struct Impl<fmt::Display, Char> : ImplBase<Char> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        byte buffer[4] {};
        auto length = this->self().encode_utf8(buffer);
        return formatter.pad(ref<str>::from_raw_parts_unchecked(buffer, length));
    }
};
} // namespace rstd

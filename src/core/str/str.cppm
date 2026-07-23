module;
#include <array>
#include <rstd/macro.hpp>
export module rstd.core:str.str;
import :num.types;
export import :slice;
export import :fmt;
export import :marker;
export import :char_;
export import :hash;

namespace rstd::str_
{
/// An unsized UTF-8 string type, analogous to Rust's `str`.
export struct Str {
    ~Str() = delete;
};

} // namespace rstd::str_

namespace rstd
{

template<>
struct Impl<Sized, str_::Str> {
    ~Impl() = delete;
};

template<>
struct Impl<ptr_::Pointee, str_::Str> {
    using Metadata = usize;
};

template<>
struct ref<str_::Str> {
public:
    USE_TRAIT(ref)

    using value_type = byte const;

    byte const* p { nullptr };
    usize       length {};

    constexpr ref() noexcept = default;

    static constexpr auto from_raw_parts_unchecked(value_type* p [[clang::lifetimebound]],
                                                   usize       length) noexcept -> Self {
        Self result;
        result.p      = p;
        result.length = length;
        return result;
    }

    constexpr auto size() const { return length; }
    constexpr auto len() const noexcept -> usize { return length; }
    constexpr auto is_empty() const noexcept -> bool { return length == usize(); }
    constexpr auto data() const { return p; }
    constexpr auto operator[](usize index) const noexcept -> u8 {
        return u8::from_byte(p[index.to_primitive()]);
    }

    constexpr auto begin() const -> ptr<u8> { return ptr<u8>::from_raw_parts(p); }
    constexpr auto end() const -> ptr<u8> { return begin().add(length); }

    constexpr auto     metadata() const noexcept -> usize { return length; }
    constexpr explicit operator bool() const { return p != nullptr; }

    constexpr auto get() const noexcept -> Self { return *this; }
    constexpr auto deref() const noexcept -> ref<str_::Str> { return *this; }
    constexpr auto operator->() const noexcept -> Self const* { return this; }
};

template<>
class mut_ref<str_::Str> {
public:
    USE_TRAIT(mut_ref)

    constexpr mut_ref() noexcept = default;

    static constexpr auto from_raw_parts_unchecked(byte* p [[clang::lifetimebound]],
                                                   usize length) noexcept -> Self {
        Self result;
        result.p_      = p;
        result.length_ = length;
        return result;
    }

    constexpr auto size() const noexcept -> usize { return length_; }
    constexpr auto len() const noexcept -> usize { return length_; }
    constexpr auto is_empty() const noexcept -> bool { return length_ == usize(); }
    constexpr auto data() const noexcept -> byte const* { return p_; }
    constexpr auto operator[](usize index) const noexcept -> u8 {
        return u8::from_byte(p_[index.to_primitive()]);
    }

    constexpr auto begin() const noexcept -> ptr<u8> { return ptr<u8>::from_raw_parts(p_); }
    constexpr auto end() const noexcept -> ptr<u8> { return begin().add(length_); }

    constexpr auto     metadata() const noexcept -> usize { return length_; }
    constexpr explicit operator bool() const noexcept { return p_ != nullptr; }

    constexpr auto as_ref() const noexcept -> ref<str_::Str> {
        return ref<str_::Str>::from_raw_parts_unchecked(p_, length_);
    }

    constexpr auto get() const noexcept -> ref<str_::Str> { return as_ref(); }
    constexpr auto get_mut() noexcept -> Self { return *this; }
    constexpr auto deref() const noexcept -> ref<str_::Str> { return as_ref(); }
    constexpr auto deref_mut() noexcept -> Self { return *this; }

    constexpr auto operator->() noexcept -> Self* { return this; }
    constexpr auto operator->() const noexcept -> Self const* { return this; }

    constexpr void make_ascii_lowercase() noexcept {
        for (rstd::size_t index = 0; index < length_.to_primitive(); ++index) {
            auto const value = u8::from_byte(p_[index]).to_primitive();
            if (value >= 'A' && value <= 'Z') {
                p_[index] = byte { static_cast<rstd::uint8_t>(value + ('a' - 'A')) };
            }
        }
    }

private:
    byte* p_ { nullptr };
    usize length_ {};
};

/// Type alias for the unsized string type.
export using str = str_::Str;

/// Compares two string slices for equality by value.
export [[nodiscard]]
constexpr bool operator==(ref<str> a, ref<str> b) noexcept {
    return a.size() == b.size() &&
           __builtin_memcmp(a.data(), b.data(), a.size().to_primitive()) == 0;
}

template<>
struct ptr<str_::Str> : ptr_base<ptr<str_::Str>, byte[], false> {
public:
    byte const* p { nullptr };
    usize       length {};

    using value_type         = byte;
    using Self               = ptr;
    constexpr ptr() noexcept = default;

    static constexpr auto from_raw_parts_unchecked(value_type* p [[clang::lifetimebound]],
                                                   usize       length) noexcept -> Self {
        Self result;
        result.p      = p;
        result.length = length;
        return result;
    }

    constexpr auto size() const { return length; }
    constexpr auto data() const { return p; }
    constexpr auto operator[](usize index) const noexcept -> u8 {
        return u8::from_byte(p[index.to_primitive()]);
    }
    constexpr auto begin() const -> ptr<u8> { return ptr<u8>::from_raw_parts(p); }
    constexpr auto end() const -> ptr<u8> { return begin().add(length); }

    constexpr explicit operator bool() const { return p != nullptr; }
};

} // namespace rstd

// ── Chars: UTF-8 code point iterator ─────────────────────────────────────
namespace rstd::str_
{

/// A hand-rolled iterator over Unicode code points in a UTF-8 string slice.
///
/// Supports `next()` for manual iteration and `begin()`/`end()` for range-for.
export struct Chars {
    byte const* _ptr;
    byte const* _end;

    /// Returns `true` if there are no remaining code points.
    constexpr auto is_empty() const noexcept -> bool { return _ptr >= _end; }

    /// Decodes and returns the next code point, advancing the position.
    /// Returns `char_::REPLACEMENT` with no advance if already at end.
    /// Use `is_empty()` to check before calling.
    constexpr auto next_unchecked() noexcept -> char32_t {
        auto [cp, n] = char_::decode_utf8(_ptr, usize(_end - _ptr));
        _ptr += n.to_primitive();
        return cp;
    }

    /// Returns the unconsumed portion of the string.
    constexpr auto as_str() const noexcept -> ref<str> {
        return ref<str>::from_raw_parts_unchecked(_ptr, usize(_end - _ptr));
    }

    // ── range-for support ────────────────────────────────────────────
    struct Sentinel {};

    struct Iterator {
        Chars*   chars;
        char32_t current { 0 };
        bool     done { false };

        constexpr Iterator(Chars* c): chars(c) { advance(); }

        constexpr void advance() {
            if (chars->_ptr >= chars->_end) {
                done = true;
                return;
            }
            current = chars->next_unchecked();
        }

        constexpr auto operator*() const -> char32_t { return current; }
        constexpr auto operator++() -> Iterator& {
            advance();
            return *this;
        }
        constexpr auto operator!=(Sentinel) const -> bool { return ! done; }
    };

    constexpr auto begin() -> Iterator { return Iterator { this }; }
    constexpr auto end() -> Sentinel { return {}; }
};

} // namespace rstd::str_

// ── ref<str> additional methods (no Option dependency) ───────────────────
namespace rstd
{

/// Creates a string slice from a byte slice without UTF-8 validation.
export constexpr auto from_utf8_unchecked(slice<u8> bytes [[clang::lifetimebound]]) noexcept
    -> ref<str> {
    return ref<str>::from_raw_parts_unchecked(bytes.as_raw_ptr(), bytes.len());
}

export constexpr auto from_utf8_unchecked_mut(mut_ref<u8[]> bytes [[clang::lifetimebound]]) noexcept
    -> mut_ref<str> {
    return mut_ref<str>::from_raw_parts_unchecked(bytes.as_raw_ptr(), bytes.len());
}

} // namespace rstd

namespace rstd::str_
{

/// Returns `true` if the string is empty (zero bytes).
export constexpr auto is_empty(ref<str> s) noexcept -> bool {
    return s.size() == usize();
}

/// Returns `true` if all bytes are ASCII.
export constexpr auto is_ascii(ref<str> s) noexcept -> bool {
    for (rstd::size_t i = 0; i < s.size().to_primitive(); ++i) {
        if (u8::from_byte(s.data()[i]).to_primitive() > 0x7F) return false;
    }
    return true;
}

/// Returns `true` if `pos` is on a UTF-8 character boundary.
export constexpr auto is_char_boundary(ref<str> s, usize pos) noexcept -> bool {
    return char_::is_char_boundary(s.data(), s.size(), pos);
}

/// Returns the UTF-8 byte slice of the string.
export constexpr auto as_bytes(ref<str> s [[clang::lifetimebound]]) noexcept -> slice<u8> {
    if (s.size() == usize()) return {};
    return slice<u8>::from_raw_parts(s.data(), s.size());
}

/// Returns a `Chars` iterator over the string's Unicode code points.
export constexpr auto chars(ref<str> s [[clang::lifetimebound]]) noexcept -> Chars {
    return { s.data(), s.data() + s.size().to_primitive() };
}

/// Returns `true` if `needle` is a substring of `haystack`.
export constexpr auto contains(ref<str> haystack, ref<str> needle) noexcept -> bool {
    if (needle.size() == usize()) return true;
    if (needle.size() > haystack.size()) return false;
    auto const limit = (haystack.size() - needle.size()).to_primitive();
    for (rstd::size_t i = 0; i <= limit; ++i) {
        if (__builtin_memcmp(haystack.data() + i, needle.data(), needle.size().to_primitive()) == 0)
            return true;
    }
    return false;
}

/// Returns `true` if the string starts with `prefix`.
export constexpr auto starts_with(ref<str> s, ref<str> prefix) noexcept -> bool {
    if (prefix.size() > s.size()) return false;
    return __builtin_memcmp(s.data(), prefix.data(), prefix.size().to_primitive()) == 0;
}

/// Returns `true` if the string ends with `suffix`.
export constexpr auto ends_with(ref<str> s, ref<str> suffix) noexcept -> bool {
    if (suffix.size() > s.size()) return false;
    return __builtin_memcmp(s.data() + (s.size() - suffix.size()).to_primitive(),
                            suffix.data(),
                            suffix.size().to_primitive()) == 0;
}

/// Splits the string at the given byte position.
export constexpr auto split_at(ref<str> s [[clang::lifetimebound]], usize mid) noexcept
    -> rstd::tuple<ref<str>, ref<str>> {
    if (! is_char_boundary(s, mid)) __builtin_trap();
    return { ref<str>::from_raw_parts_unchecked(s.data(), mid),
             ref<str>::from_raw_parts_unchecked(s.data() + mid.to_primitive(), s.size() - mid) };
}

/// Returns the string with leading and trailing ASCII whitespace removed.
export constexpr auto trim(ref<str> s [[clang::lifetimebound]]) noexcept -> ref<str> {
    auto* b             = s.data();
    auto* e             = b + s.size().to_primitive();
    auto  is_whitespace = [](byte value) constexpr {
        auto raw = u8::from_byte(value).to_primitive();
        return raw == ' ' || raw == '\t' || raw == '\n' || raw == '\r';
    };
    while (b < e && is_whitespace(*b)) ++b;
    while (e > b && is_whitespace(*(e - 1))) --e;
    return ref<str>::from_raw_parts_unchecked(b, usize(e - b));
}

/// Extracts the last `count` path components from a path string.
/// \param path The path string to extract from.
/// \param count The number of trailing path components to extract.
/// \return A string slice containing the last `count` components.
export constexpr auto extract_last(ref<str> path [[clang::lifetimebound]], usize count)
    -> ref<str> {
    auto pos = path.size();
    while (pos != usize()) {
        auto const index = (pos - usize(1)).to_primitive();
        auto const value = u8::from_byte(path.data()[index]).to_primitive();
        if (value == '/' || value == '\\') {
            --count;
        }
        if (count != usize()) {
            --pos;
        } else {
            break;
        }
    }
    auto begin = path.begin() + pos.to_primitive();
    auto size  = static_cast<rstd::size_t>(path.end() - begin);
    return ref<str>::from_raw_parts_unchecked(begin, usize(size));
}
} // namespace rstd::str_

namespace rstd
{

template<>
struct Impl<fmt::Display, ref<str>> : ImplBase<ref<str>> {
    auto fmt(fmt::Formatter& f) const -> bool { return f.pad(this->self()); }
};

template<>
struct Impl<fmt::Debug, ref<str>> : ImplBase<ref<str>> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto const* quote = reinterpret_cast<rstd::uint8_t const*>("\"");
        if (! f.write_raw(quote, rstd::size_t(1))) return false;
        if (! f.write_raw(this->self().data(), this->self().size().to_primitive())) return false;
        return f.write_raw(quote, rstd::size_t(1));
    }
};

template<>
struct Impl<hash::Hash, ref<str>> : ImplBase<ref<str>> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        rstd::as<hash::Hasher>(state).write(str_::as_bytes(this->self()));
        byte const separator { 0xff };
        rstd::as<hash::Hasher>(state).write(
            slice<u8>::from_raw_parts(rstd::addressof(separator), usize(1)));
    }
};

} // namespace rstd

export namespace rstd::str_
{

template<rstd::size_t N>
struct fixed_string {
    char data[N];

    consteval fixed_string(const char (&source)[N]) {
        for (rstd::size_t index = 0; index < N; ++index) data[index] = source[index];
    }

    static constexpr auto size() noexcept -> rstd::size_t { return N - 1; }
};

template<fixed_string Str>
consteval auto make_byte_literal() -> std::array<byte, Str.size()> {
    std::array<byte, Str.size()> result {};
    for (rstd::size_t index = 0; index < Str.size(); ++index) {
        result[index] = byte { static_cast<unsigned char>(Str.data[index]) };
    }
    return result;
}

template<fixed_string Str>
inline constexpr auto BYTE_LITERAL_STORAGE = make_byte_literal<Str>();

template<fixed_string Str>
inline constexpr bool VALID_UTF8_LITERAL =
    char_::is_valid_utf8(BYTE_LITERAL_STORAGE<Str>.data(), usize(BYTE_LITERAL_STORAGE<Str>.size()));

} // namespace rstd::str_

export namespace rstd::literals
{

template<str_::fixed_string Str>
consteval auto operator""_b() {
    return str_::make_byte_literal<Str>();
}

template<str_::fixed_string Str>
consteval auto operator""_bytes() -> slice<u8> {
    constexpr auto& storage = str_::BYTE_LITERAL_STORAGE<Str>;
    return slice<u8>::from_raw_parts(storage.data(), usize(storage.size()));
}

template<str_::fixed_string Str>
consteval auto operator""_str() -> ref<str> {
    static_assert(str_::VALID_UTF8_LITERAL<Str>, "rstd string literal must be valid UTF-8");
    constexpr auto& storage = str_::BYTE_LITERAL_STORAGE<Str>;
    return ref<str>::from_raw_parts_unchecked(storage.data(), usize(storage.size()));
}

} // namespace rstd::literals

module;
#include <array>
#include <rstd/macro.hpp>
export module rstd.core:str.str;
import :num.types;
import :intrinsics;
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

export struct Bytes;
export struct Chars;
export struct CharIndices;

} // namespace rstd::str_

namespace rstd
{

/// Type alias for the unsized string type.
export using str = str_::Str;

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
    constexpr auto end() const -> ptr<u8> {
        if (is_empty()) return begin();
        return begin().add(length);
    }

    constexpr auto is_ascii() const noexcept -> bool {
        for (rstd::size_t index = 0; index < length.to_primitive(); ++index) {
            if (u8::from_byte(p[index]).to_primitive() > 0x7f) return false;
        }
        return true;
    }

    constexpr auto is_char_boundary(usize index) const noexcept -> bool {
        return char_::is_char_boundary(p, length, index);
    }

    constexpr auto as_bytes() const noexcept -> slice<u8> {
        if (is_empty()) return {};
        return slice<u8>::from_raw_parts(p, length);
    }

    constexpr auto bytes() const noexcept -> str_::Bytes;
    constexpr auto chars() const noexcept -> str_::Chars;
    constexpr auto char_indices() const noexcept -> str_::CharIndices;

    constexpr auto contains(ref<str> pattern) const noexcept -> bool {
        if (pattern.is_empty()) return true;
        if (pattern.len() > length) return false;
        auto const limit = (length - pattern.len()).to_primitive();
        for (rstd::size_t index = 0; index <= limit; ++index) {
            auto matches = true;
            for (rstd::size_t pattern_index = 0; pattern_index < pattern.len().to_primitive();
                 ++pattern_index) {
                if (p[index + pattern_index] != pattern.data()[pattern_index]) {
                    matches = false;
                    break;
                }
            }
            if (matches) return true;
        }
        return false;
    }

    constexpr auto starts_with(ref<str> pattern) const noexcept -> bool {
        if (pattern.is_empty()) return true;
        if (pattern.len() > length) return false;
        for (rstd::size_t index = 0; index < pattern.len().to_primitive(); ++index) {
            if (p[index] != pattern.data()[index]) return false;
        }
        return true;
    }

    constexpr auto ends_with(ref<str> pattern) const noexcept -> bool {
        if (pattern.is_empty()) return true;
        if (pattern.len() > length) return false;
        auto const offset = (length - pattern.len()).to_primitive();
        for (rstd::size_t index = 0; index < pattern.len().to_primitive(); ++index) {
            if (p[offset + index] != pattern.data()[index]) return false;
        }
        return true;
    }

    constexpr auto find(ref<str> pattern) const noexcept -> Option<usize>;
    constexpr auto rfind(ref<str> pattern) const noexcept -> Option<usize>;
    constexpr auto get(usize start, usize end) const noexcept -> Option<ref<str>>;
    constexpr auto strip_prefix(ref<str> pattern) const noexcept -> Option<ref<str>>;
    constexpr auto strip_suffix(ref<str> pattern) const noexcept -> Option<ref<str>>;
    constexpr auto split_once(ref<str> pattern) const noexcept -> Option<tuple<ref<str>, ref<str>>>;
    constexpr auto rsplit_once(ref<str> pattern) const noexcept
        -> Option<tuple<ref<str>, ref<str>>>;

    constexpr auto split_at(usize index) const noexcept -> tuple<ref<str>, ref<str>> {
        if (! is_char_boundary(index)) rstd::intrinsics::abort();
        auto* right = p;
        if (index != usize()) right += index.to_primitive();
        return { ref<str>::from_raw_parts_unchecked(p, index),
                 ref<str>::from_raw_parts_unchecked(right, length - index) };
    }

    constexpr auto trim_ascii() const noexcept -> ref<str> {
        auto is_whitespace = [](byte value) constexpr {
            auto const raw = u8::from_byte(value).to_primitive();
            return raw == 0x09 || raw == 0x0a || raw == 0x0c || raw == 0x0d || raw == 0x20;
        };
        rstd::size_t begin_index = 0;
        auto         end_index   = length.to_primitive();
        while (begin_index < end_index && is_whitespace(p[begin_index])) ++begin_index;
        while (end_index > begin_index && is_whitespace(p[end_index - 1])) --end_index;
        auto* begin_ptr = p;
        if (begin_index != 0) begin_ptr += begin_index;
        return ref<str>::from_raw_parts_unchecked(begin_ptr, usize(end_index - begin_index));
    }

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
    constexpr auto end() const noexcept -> ptr<u8> {
        if (is_empty()) return begin();
        return begin().add(length_);
    }

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

    constexpr auto is_ascii() const noexcept -> bool { return as_ref().is_ascii(); }
    constexpr auto is_char_boundary(usize index) const noexcept -> bool {
        return as_ref().is_char_boundary(index);
    }
    constexpr auto as_bytes() const noexcept -> slice<u8> { return as_ref().as_bytes(); }
    constexpr auto bytes() const noexcept -> str_::Bytes;
    constexpr auto chars() const noexcept -> str_::Chars;
    constexpr auto char_indices() const noexcept -> str_::CharIndices;
    constexpr auto contains(ref<str> pattern) const noexcept -> bool {
        return as_ref().contains(pattern);
    }
    constexpr auto starts_with(ref<str> pattern) const noexcept -> bool {
        return as_ref().starts_with(pattern);
    }
    constexpr auto ends_with(ref<str> pattern) const noexcept -> bool {
        return as_ref().ends_with(pattern);
    }
    constexpr auto find(ref<str> pattern) const noexcept -> Option<usize>;
    constexpr auto rfind(ref<str> pattern) const noexcept -> Option<usize>;
    constexpr auto get(usize start, usize end) const noexcept -> Option<ref<str>>;
    constexpr auto strip_prefix(ref<str> pattern) const noexcept -> Option<ref<str>>;
    constexpr auto strip_suffix(ref<str> pattern) const noexcept -> Option<ref<str>>;
    constexpr auto split_once(ref<str> pattern) const noexcept -> Option<tuple<ref<str>, ref<str>>>;
    constexpr auto rsplit_once(ref<str> pattern) const noexcept
        -> Option<tuple<ref<str>, ref<str>>>;
    constexpr auto split_at(usize index) const noexcept -> tuple<ref<str>, ref<str>> {
        return as_ref().split_at(index);
    }
    constexpr auto trim_ascii() const noexcept -> ref<str> { return as_ref().trim_ascii(); }

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

/// Compares two string slices for equality by value.
export [[nodiscard]]
constexpr bool operator==(ref<str> a, ref<str> b) noexcept {
    if (a.size() != b.size()) return false;
    for (rstd::size_t index = 0; index < a.size().to_primitive(); ++index) {
        if (a.data()[index] != b.data()[index]) return false;
    }
    return true;
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
    constexpr auto end() const -> ptr<u8> {
        if (length == usize()) return begin();
        return begin().add(length);
    }

    constexpr explicit operator bool() const { return p != nullptr; }
};

} // namespace rstd

namespace rstd::str_
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
        rstd::as<hash::Hasher>(state).write(this->self().as_bytes());
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

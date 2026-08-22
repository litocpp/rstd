module;
#include <rstd/macro.hpp>
export module rstd.alloc:string;
export import :vec;
export import rstd.core;

using ::alloc::vec::Vec;

namespace ffi = rstd::ffi;
using namespace rstd::prelude;

namespace alloc::string
{

/// An error that retains the original bytes when UTF-8 validation fails.
export class FromUtf8Error {
    Vec<u8>               bytes_;
    rstd::str_::Utf8Error error_;

public:
    /// Creates an error from the rejected bytes and their validation error.
    FromUtf8Error(Vec<u8>&& bytes, rstd::str_::Utf8Error error)
        : bytes_(rstd::move(bytes)), error_(error) {}

    /// Returns the rejected byte sequence.
    auto as_bytes() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        return bytes_.as_slice();
    }

    /// Recovers ownership of the rejected byte sequence.
    auto into_bytes() && -> Vec<u8> { return rstd::move(bytes_); }
    /// Returns the UTF-8 validation error.
    constexpr auto utf8_error() const noexcept -> rstd::str_::Utf8Error { return error_; }
};

/// A UTF-8 encoded, growable string, analogous to Rust's `String`.
export class String {
    Vec<u8> vec;

    constexpr String(Vec<u8>&& p): vec(rstd::move(p)) {}

public:
    USE_TRAIT(String)
    constexpr String()                 = default;
    constexpr String(Self&&) noexcept  = default;
    String& operator=(Self&&) noexcept = default;

    using value_type = u8;

    /// Creates a new empty `String`.
    static auto make() -> String { return {}; }

    /// Creates a `String` from a string slice (copies the bytes).
    static auto make(ref<str> s) -> String;

    auto clone() const -> String;

    void clone_from(const String& source);

    /// Creates a new `String` from a byte vector without checking UTF-8 validity.
    static auto from_utf8_unchecked(Vec<u8>&& bytes) -> String;

    /// Creates a new `String` from owned bytes after validating UTF-8.
    static auto from_utf8(Vec<u8>&& bytes) -> Result<String, FromUtf8Error>;

    /// Converts the `String` to a `ref<str>` string slice.
    operator ref<str>() const [[clang::lifetimebound]];

    /// Appends a UTF-8 string slice.
    void push_str(ref<str> value);

    /// Appends one ASCII byte while preserving the UTF-8 invariant.
    void push_ascii(u8 value);

    void push_ascii(char value);

    /// Appends a Unicode code point, encoding as UTF-8.
    void push(char32_t cp);

    /// Returns a string slice of the entire `String`.
    auto as_str() const noexcept [[clang::lifetimebound]] -> ref<str>;

    constexpr auto as_mut_str() & noexcept [[clang::lifetimebound]] -> mut_ref<str> {
        return rstd::str_::from_utf8_unchecked_mut(vec.as_mut_slice().as_mut_ref());
    }

    /// Returns the byte length of this string.
    constexpr auto len() const noexcept -> usize { return vec.len(); }
    /// Returns `true` if this string contains no bytes.
    constexpr auto is_empty() const noexcept -> bool { return vec.len() == usize(); }
    /// Returns the current capacity in bytes.
    constexpr auto capacity() const noexcept -> usize { return vec.capacity(); }
    /// Ensures that at least `additional` more bytes can be inserted without reallocating.
    void reserve(usize additional);
    /// Clears the string, removing all bytes.
    constexpr void clear() { vec.clear(); }

    /// Truncates the string to `new_len` bytes.
    ///
    /// Panics if `new_len` is not on a UTF-8 character boundary.
    void truncate(usize new_len);

    /// Replaces the UTF-8 byte range `[start, end)` with `replacement`.
    void replace_range(usize start, usize end, ref<str> replacement);

    /// Inserts a UTF-8 string slice at a checked byte boundary.
    void insert_str(usize index, ref<str> value);

    /// Inserts a Unicode code point at a checked byte boundary.
    void insert(usize index, char32_t code_point);

    friend constexpr auto operator<=>(const String& a, const String& b) noexcept {
        return rstd::lexicographical_compare_three_way(
            a.vec.begin(), a.vec.end(), b.vec.begin(), b.vec.end());
    }
    friend constexpr auto operator<=>(const String& a, slice<u8> b) noexcept {
        return rstd::lexicographical_compare_three_way(
            a.vec.begin(), a.vec.end(), b.begin(), b.end());
    }
    friend auto operator<=>(const String& a, ref<str> b) noexcept {
        auto a_str = a.as_str();
        return rstd::lexicographical_compare_three_way(
            a_str.begin(), a_str.end(), b.begin(), b.end());
    }
    friend auto operator<=>(ref<str> a, const String& b) noexcept {
        auto b_str = b.as_str();
        return rstd::lexicographical_compare_three_way(
            a.begin(), a.end(), b_str.begin(), b_str.end());
    }
    friend bool operator==(const String& a, ref<str> b) noexcept { return a.as_str() == b; }
    friend bool operator==(ref<str> a, const String& b) noexcept { return b == a; }
    /// Returns a raw pointer to the underlying byte buffer.
    /// \return A const pointer to the first byte.
    constexpr auto as_raw_ptr() const noexcept [[clang::lifetimebound]] -> const byte* {
        return vec.data();
    }

    /// Returns a const iterator to the beginning of the string.
    auto begin() const noexcept [[clang::lifetimebound]] -> ptr<u8>;
    /// Returns a const iterator to the end of the string.
    auto end() const noexcept [[clang::lifetimebound]] -> ptr<u8>;
    /// Returns a pointer to the underlying byte storage.
    /// \return A const `byte*` pointer to the data.
    auto data() const noexcept [[clang::lifetimebound]] -> const byte*;
    /// Returns the length of the string in bytes.
    /// \return The number of bytes in the string.
    constexpr auto size() const noexcept -> usize { return vec.len(); }

    auto into_bytes() && -> Vec<u8>;
};

/// A trait for converting a value to a `String`.
export struct ToString {
    template<typename T, typename = void>
    struct Api {
        auto to_string() const -> String;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::to_string>;
};

} // namespace alloc::string

using ::alloc::string::String;
using ::alloc::string::ToString;

namespace rstd
{
template<>
struct Impl<iter::Extend<char32_t>, String> : ImplBase<String> {
    template<iter::has_next It>
    static void extend(String& string, It iterator) {
        for (auto item = iterator.next(); item.is_some(); item = iterator.next())
            string.push(rstd::move(*item));
    }

    static void extend_one(String& string, char32_t&& item) { string.push(item); }
};

template<>
struct Impl<iter::FromIterator<char32_t>, String> : ImplBase<String> {
    template<iter::has_next It>
    static auto from_iter(It iterator) -> String {
        auto string = String::make();
        Impl<iter::Extend<char32_t>, String>::extend(string, rstd::move(iterator));
        return string;
    }
};

template<>
struct Impl<ops::Deref, String> : ImplBase<String> {
    using Target = str;

    auto deref() const noexcept -> ref<Target>;
};

template<>
struct Impl<ops::DerefMut, String> : ImplBase<String> {
    auto deref_mut() noexcept -> mut_ref<ops::deref_target_t<String>>;
};

template<>
struct Impl<hash::Hash, String> : ImplBase<String> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().as_str(), state);
    }
};

template<>
struct Impl<borrow::Borrow<str>, String> : ImplBase<String> {
    auto borrow() const noexcept -> ref<str>;
};

template<>
struct Impl<fmt::Write, String> : ImplBase<String> {
    auto write_str(ref<str> const& value) -> bool;
};

template<>
struct Impl<fmt::Display, String> : ImplBase<String> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, String> : ImplBase<String> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Display, ::alloc::string::FromUtf8Error>
    : ImplBase<::alloc::string::FromUtf8Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, ::alloc::string::FromUtf8Error> : ImplBase<::alloc::string::FromUtf8Error> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<error::Error, ::alloc::string::FromUtf8Error>
    : DefaultInImpl<error::Error, ::alloc::string::FromUtf8Error> {};

} // namespace rstd

namespace rstd::fmt
{

/// Creates a `String` using format syntax, analogous to Rust's `format!` macro.
/// \tparam Args The types of the format arguments.
/// \param fmt_str The format string.
/// \param args The arguments to format.
/// \return A `String` with the formatted result.
export template<typename... Args>
auto format(fmt::format_string<Args...> fmt_str, Args&&... args) -> String {
    auto      buf = String::make();
    Formatter f(buf);
    if constexpr (sizeof...(Args) > 0) {
        Argument arg_array[] = { Argument::make(args)... };
        f.write_fmt({ fmt_str.data(), fmt_str.size(), arg_array, sizeof...(Args) });
    } else {
        f.write_fmt({ fmt_str.data(), fmt_str.size(), nullptr, 0 });
    }
    return buf;
}

} // namespace rstd::fmt

namespace rstd
{

/// Re-exports `fmt::format` into the `rstd` namespace.
export using fmt::format;

template<typename T>
    requires(num::Integer<T> || rstd::is_raw_int<T>)
struct Impl<fmt::Display, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& f) const -> bool {
        char  buf[64];
        char* p   = buf + 64;
        auto  raw = [&]() {
            if constexpr (num::Integer<T>)
                return this->self().to_primitive();
            else
                return this->self();
        }();
        if (raw == 0) {
            constexpr rstd::uint8_t ZERO[] = { '0' };
            return f.write_raw(ZERO, sizeof(ZERO));
        }
        using Raw = decltype(raw);
        rstd::uint128_t magnitude;
        bool            negative = false;
        if constexpr (Raw(-1) < Raw(0)) {
            if (raw < 0) {
                negative  = true;
                magnitude = static_cast<rstd::uint128_t>(0) - static_cast<rstd::uint128_t>(raw);
            } else {
                magnitude = static_cast<rstd::uint128_t>(raw);
            }
        } else {
            magnitude = static_cast<rstd::uint128_t>(raw);
        }

        while (magnitude > 0) {
            *--p = char('0' + static_cast<unsigned>(magnitude % 10));
            magnitude /= 10;
        }

        if (negative) *--p = '-';
        return f.write_raw(reinterpret_cast<const rstd::uint8_t*>(p),
                           static_cast<rstd::size_t>((buf + 64) - p));
    }
};

template<typename T>
    requires(num::Integer<T> || rstd::is_raw_int<T>)
struct Impl<fmt::Debug, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& f) const -> bool { return as<fmt::Display>(this->self()).fmt(f); }
};

template<>
struct Impl<fmt::Display, char const*> : ImplBase<char const*> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<>
struct Impl<fmt::Debug, char const*> : ImplBase<char const*> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<rstd::size_t N>
struct Impl<fmt::Display, char[N]> : ImplBase<char[N]> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return f.write_raw(reinterpret_cast<const rstd::uint8_t*>(this->self()),
                           rstd::strlen(this->self()));
    }
};

template<rstd::size_t N>
struct Impl<fmt::Display, char const[N]> : ImplBase<char const[N]> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return f.write_raw(reinterpret_cast<const rstd::uint8_t*>(this->self()),
                           rstd::strlen(this->self()));
    }
};

// Duration has no Display (matching Rust: format is ambiguous).
// Debug format matches Rust: "1.5s", "500ms", "1.234µs", "789ns".
template<>
struct Impl<fmt::Debug, time::Duration> : ImplBase<time::Duration> {
    auto fmt(fmt::Formatter& formatter) const -> bool;
};

template<mtp::same_as<ToString> T, Impled<fmt::Display> A>
struct Impl<T, A> : ImplBase<A> {
    auto to_string() const -> String { return rstd::format("{}", this->self()); }
};

template<mtp::same_as<cmp::PartialEq<String>> T, mtp::same_as<String> A>
struct Impl<T, A> : DefaultInImpl<T, A> {
    auto eq(const String& other) const noexcept -> bool {
        return this->self().size() == other.size() &&
               rstd::mem::memcmp(this->self().begin(), other.begin(), this->self().size()) == 0;
    }
};

template<mtp::same_as<cmp::PartialEq<char const*>> T, mtp::same_as<String> A>
struct Impl<T, A> : DefaultInImpl<T, A> {
    using Rhs = char const*;
    auto eq(const Rhs& other) const noexcept -> bool {
        auto& a = this->self();
        return rstd::lexicographical_compare_three_way(
                   a.begin(), a.end(), other, other + rstd::strlen(other)) ==
               rstd::strong_ordering::equal;
    }
};

template<mtp::same_as<Into<Vec<u8>>> T, mtp::same_as<String> A>
struct Impl<T, A> : ImplBase<A> {
    auto into() -> Vec<u8> { return rstd::move(this->self()).into_bytes(); }
};

/// Converts a value that implements `ToString` into a `String`.
/// \tparam A The type of the value, which must implement `ToString`.
/// \param a The value to convert.
/// \return A `String` representation of the value.
export template<Impled<ToString> A>
auto to_string(A&& a) {
    return as<ToString>(a).to_string();
}

} // namespace rstd

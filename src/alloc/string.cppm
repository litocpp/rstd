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

/// Iterator over the Unicode scalar values of a UTF-8 byte range.
export struct Chars : rstd::DefaultInClass<Chars, rstd::iter::Iterator> {
    using Item = u32;
    rstd::str_::Chars inner;

    explicit Chars(rstd::str_::Chars chars): inner(chars) {}

    auto next() -> rstd::Option<u32> {
        if (inner.is_empty()) return rstd::None();
        return rstd::Some(u32(inner.next_unchecked()));
    }
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
    static auto make(ref<str> s) -> String {
        return String { Vec<u8>::copy_from_bytes(rstd::str_::as_bytes(s)) };
    }

    /// Creates a `String` from a null-terminated C string (copies the bytes).
    static auto make(const char* s) -> String { return make(ref<str>(s)); }

    auto clone() const -> String { return String::make(as_str()); }

    void clone_from(String& source) { *this = source.clone(); }

    /// Creates a new `String` from a byte vector without checking UTF-8 validity.
    static auto from_utf8_unchecked(Vec<u8>&& bytes) -> String {
        return String { rstd::move(bytes) };
    }

    /// Creates a new `String` from owned bytes after validating UTF-8.
    static auto from_utf8(Vec<u8>&& bytes) -> Result<String, rstd::str_::Utf8Error> {
        auto validation = rstd::str_::validate_utf8(bytes.as_slice());
        if (validation.is_err()) return Err(rstd::move(validation).unwrap_err());
        return Ok(String { rstd::move(bytes) });
    }

    /// Returns a reference to the string as a `CStr`.
    /// \return A `ref<CStr>` view of the string data.
    auto as_ref() const noexcept [[clang::lifetimebound]] -> ref<ffi::CStr> {
        auto p = reinterpret_cast<char const*>(vec.as_ptr().as_raw_ptr());
        return ref<ffi::CStr>::from_raw_parts(p, vec.len());
    }

    /// Converts the `String` to a `ref<str>` string slice.
    operator ref<str>() const [[clang::lifetimebound]] { return as_str(); }

    /// Appends a `char` to the end of this string.
    void push_back(char c) { vec.push(u8(c)); }
    /// Appends a byte to the end of this string.
    void push_back(u8 c) { vec.push(rstd::move(c)); }

    /// Appends a UTF-8 string slice.
    void push_str(ref<str> value) {
        if (value.size() == usize()) return;
        vec.extend_from_bytes(rstd::str_::as_bytes(value));
    }

    /// Appends a Unicode code point, encoding as UTF-8.
    void push(char32_t cp) {
        u8   buf[4];
        auto n = rstd::char_::encode_utf8(cp, buf);
        for (rstd::size_t index = 0; index < n.to_primitive(); ++index) {
            u8 b = buf[index];
            vec.push(rstd::move(b));
        }
    }

    /// Returns a string slice of the entire `String`.
    auto as_str() const noexcept [[clang::lifetimebound]] -> ref<str> {
        return rstd::from_utf8_unchecked(vec.as_slice());
    }

    /// Returns the byte length of this string.
    constexpr auto len() const noexcept -> usize { return vec.len(); }
    /// Returns `true` if this string contains no bytes.
    constexpr auto is_empty() const noexcept -> bool { return vec.len() == usize(); }
    /// Returns the current capacity in bytes.
    constexpr auto capacity() const noexcept -> usize { return vec.capacity(); }
    /// Ensures that at least `additional` more bytes can be inserted without reallocating.
    void reserve(usize additional) { vec.reserve(additional); }
    /// Clears the string, removing all bytes.
    constexpr void clear() { vec.clear(); }

    /// Truncates the string to `new_len` bytes.
    ///
    /// Panics if `new_len` is not on a UTF-8 character boundary.
    void truncate(usize new_len) {
        if (new_len < vec.len()) {
            rstd_assert(rstd::char_::is_char_boundary(vec.begin(), vec.len(), new_len));
            while (vec.len() > new_len) vec.pop();
        }
    }

    /// Replaces the UTF-8 byte range `[start, end)` with `replacement`.
    void replace_range(usize start, usize end, ref<str> replacement) {
        rstd_assert(start <= end && end <= vec.len());
        auto current = as_str();
        rstd_assert(rstd::str_::is_char_boundary(current, start));
        rstd_assert(rstd::str_::is_char_boundary(current, end));

        auto result = Vec<u8>::with_capacity(start + replacement.size() + vec.len() - end);
        if (start != usize()) {
            result.extend_from_bytes(slice<byte>::from_raw_parts(current.data(), start));
        }
        result.extend_from_bytes(rstd::str_::as_bytes(replacement));
        if (end != vec.len()) {
            result.extend_from_bytes(
                slice<byte>::from_raw_parts(current.data() + end.to_primitive(), vec.len() - end));
        }
        vec = rstd::move(result);
    }

    /// Inserts a UTF-8 string slice at a checked byte boundary.
    void insert_str(usize index, ref<str> value) { replace_range(index, index, value); }

    /// Inserts a Unicode code point at a checked byte boundary.
    void insert(usize index, char32_t code_point) {
        u8   bytes[4];
        auto length = rstd::char_::encode_utf8(code_point, bytes);
        insert_str(index, ref<str>(slice<u8>::from_raw_parts(bytes, length)));
    }

    friend constexpr auto operator<=>(const String& a, const String& b) noexcept {
        return rstd::lexicographical_compare_three_way(
            a.vec.begin(), a.vec.end(), b.vec.begin(), b.vec.end());
    }
    friend constexpr auto operator<=>(const String& a, slice<u8> b) noexcept {
        auto ptr = &*b;
        return rstd::lexicographical_compare_three_way(
            a.vec.begin(), a.vec.end(), ptr, ptr + b.len().to_primitive());
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
    friend bool operator==(const String& a, ref<str> b) noexcept {
        return a.size() == b.size() && rstd::mem::memcmp(a.begin(), b.begin(), a.size()) == 0;
    }
    friend bool operator==(ref<str> a, const String& b) noexcept { return b == a; }
    friend bool operator==(char const* b, const String& a) noexcept {
        const usize length(rstd::strlen(b));
        return a.vec.len() == length && rstd::mem::memcmp(a.vec.begin(), b, length) == 0;
    }

    /// Returns a raw pointer to the underlying byte buffer.
    /// \return A const pointer to the first byte.
    constexpr auto as_raw_ptr() const noexcept [[clang::lifetimebound]] -> const u8* {
        return vec.begin();
    }

    /// Returns a const iterator to the beginning of the string.
    auto begin() const noexcept [[clang::lifetimebound]] -> const char* {
        return reinterpret_cast<const char*>(vec.begin());
    }
    /// Returns a const iterator to the end of the string.
    auto end() const noexcept [[clang::lifetimebound]] -> const char* {
        return reinterpret_cast<const char*>(vec.end());
    }
    /// Returns a pointer to the string data as a char array.
    /// \return A const `char*` pointer to the data.
    auto data() const noexcept [[clang::lifetimebound]] -> const char* {
        return reinterpret_cast<const char*>(vec.as_ptr().as_raw_ptr());
    }
    /// Returns the length of the string in bytes.
    /// \return The number of bytes in the string.
    constexpr auto size() const noexcept -> usize { return vec.len(); }

    /// Returns an iterator over the bytes (`u8`) of the string.
    auto bytes() const [[clang::lifetimebound]] {
        return rstd::iter::SliceIter<u8>(vec.begin(), vec.end()).copied();
    }
    /// Returns an iterator over the Unicode scalar values of the string.
    auto chars() const [[clang::lifetimebound]] -> Chars {
        return Chars(rstd::str_::chars(as_str()));
    }
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
struct Impl<hash::Hash, String> : ImplBase<String> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().as_str(), state);
    }
};

template<>
struct Impl<borrow::Borrow<str>, String> : ImplBase<String> {
    auto borrow() const noexcept -> ref<str> { return this->self().as_str(); }
};

template<>
struct Impl<fmt::Write, String> : ImplBase<String> {
    auto write_str(const rstd::uint8_t* p, rstd::size_t len) -> bool {
        this->self().push_str(ref<str>::from_raw_parts(p, usize(len)));
        return true;
    }
};

template<>
struct Impl<fmt::Display, String> : ImplBase<String> {
    auto fmt(fmt::Formatter& f) const -> bool { return f.pad(this->self().as_str()); }
};

template<>
struct Impl<fmt::Debug, String> : ImplBase<String> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto value = this->self().as_str();
        return as<fmt::Debug>(value).fmt(f);
    }
};

// collect<String>() from an iterator of char or u8.
template<>
struct Impl<iter::FromIterator<char>, String> : ImplBase<String> {
    template<typename It>
    static auto from_iter(It it) -> String {
        auto s = String::make();
        for (auto x = it.next(); x.is_some(); x = it.next()) s.push_back(*x);
        return s;
    }
};

template<>
struct Impl<iter::FromIterator<u8>, String> : ImplBase<String> {
    template<typename It>
    static auto from_iter(It it) -> String {
        auto s = String::make();
        for (auto x = it.next(); x.is_some(); x = it.next()) s.push_back(*x);
        return s;
    }
};

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
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self();
        return f.write_raw(reinterpret_cast<const rstd::uint8_t*>(s), rstd::strlen(s));
    }
};

template<>
struct Impl<fmt::Debug, char const*> : ImplBase<char const*> {
    auto fmt(fmt::Formatter& f) const -> bool {
        constexpr rstd::uint8_t QUOTE[] = { '"' };
        f.write_raw(QUOTE, sizeof(QUOTE));
        as<fmt::Display>(this->self()).fmt(f);
        return f.write_raw(QUOTE, sizeof(QUOTE));
    }
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
    auto fmt(fmt::Formatter& f) const -> bool {
        auto write_ascii = [&f](const char* value, rstd::size_t length) {
            return f.write_raw(reinterpret_cast<const rstd::uint8_t*>(value), length);
        };
        auto&                d     = this->self();
        const rstd::uint64_t secs  = d.as_secs().to_primitive();
        const rstd::uint32_t nanos = d.subsec_nanos().to_primitive();
        if (secs > 0) {
            // Render as seconds with up to 9 decimal places, trimming trailing zeros.
            auto s = rstd::format("{}", secs);
            write_ascii(s.data(), s.size().to_primitive());
            if (nanos != 0) {
                // Produce 9-digit fractional part then strip trailing zeros.
                char           frac[10];
                rstd::uint32_t n = nanos;
                for (int i = 8; i >= 0; --i) {
                    frac[i] = char('0' + n % 10);
                    n /= 10;
                }
                frac[9] = '\0';
                int len = 9;
                while (len > 1 && frac[len - 1] == '0') --len;
                write_ascii(".", 1);
                write_ascii(frac, static_cast<rstd::size_t>(len));
            }
            return write_ascii("s", 1);
        } else if (nanos >= time::NANOS_PER_MILLI.to_primitive()) {
            // milliseconds
            rstd::uint32_t ms  = nanos / time::NANOS_PER_MILLI.to_primitive();
            rstd::uint32_t rem = nanos % time::NANOS_PER_MILLI.to_primitive();
            auto           s   = rstd::format("{}", ms);
            write_ascii(s.data(), s.size().to_primitive());
            if (rem != 0) {
                char           frac[7];
                rstd::uint32_t r = rem;
                for (int i = 5; i >= 0; --i) {
                    frac[i] = char('0' + r % 10);
                    r /= 10;
                }
                frac[6] = '\0';
                int len = 6;
                while (len > 1 && frac[len - 1] == '0') --len;
                write_ascii(".", 1);
                write_ascii(frac, static_cast<rstd::size_t>(len));
            }
            return write_ascii("ms", 2);
        } else if (nanos >= time::NANOS_PER_MICRO.to_primitive()) {
            // microseconds — use ASCII "us" (µ is multi-byte, avoid encoding issues)
            rstd::uint32_t us  = nanos / time::NANOS_PER_MICRO.to_primitive();
            rstd::uint32_t rem = nanos % time::NANOS_PER_MICRO.to_primitive();
            auto           s   = rstd::format("{}", us);
            write_ascii(s.data(), s.size().to_primitive());
            if (rem != 0) {
                char           frac[4];
                rstd::uint32_t r = rem;
                for (int i = 2; i >= 0; --i) {
                    frac[i] = char('0' + r % 10);
                    r /= 10;
                }
                frac[3] = '\0';
                int len = 3;
                while (len > 1 && frac[len - 1] == '0') --len;
                write_ascii(".", 1);
                write_ascii(frac, static_cast<rstd::size_t>(len));
            }
            return write_ascii("us", 2);
        } else {
            auto s = rstd::format("{}", nanos);
            write_ascii(s.data(), s.size().to_primitive());
            return write_ascii("ns", 2);
        }
    }
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
    auto into() -> Vec<u8> {
        auto& a = this->self();
        return rstd::move(a.vec);
    }
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

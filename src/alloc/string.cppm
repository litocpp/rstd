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
    /// \return An empty `String`.
    static auto make() -> String { return {}; }
    /// Creates a new `String` from a byte vector without checking UTF-8 validity.
    /// \param bytes The byte vector to convert.
    /// \return A `String` wrapping the given bytes.
    static auto from_utf8_unchecked(Vec<u8>&& bytes) -> String {
        return String { rstd::move(bytes) };
    }

    /// Returns a reference to the string as a `CStr`.
    /// \return A `ref<CStr>` view of the string data.
    auto as_ref() const noexcept -> ref<ffi::CStr> {
        auto p = as_cast<ffi::CStr const*>(&*vec.as_ptr());
        return ref<ffi::CStr>::from_raw_parts(p, vec.len());
    }

    /// Converts the `String` to a `ref<str>` string slice.
    constexpr operator ref<str>() const {
        return ref<str>::from_raw_parts(&*vec.as_ptr(), vec.len());
    }

    /// Appends a `char` to the end of this string.
    /// \param c The character to append.
    void push_back(char c) { vec.push(static_cast<u8>(c)); }
    /// Appends a byte to the end of this string.
    /// \param c The byte to append.
    void push_back(u8 c) { vec.push(rstd::move(c)); }

    friend constexpr auto operator<=>(const String& a, const String& b) noexcept {
        return rstd::lexicographical_compare_three_way(
            a.vec.begin(), a.vec.end(), b.vec.begin(), b.vec.end());
    }
    friend constexpr auto operator<=>(const String& a, slice<u8> b) noexcept {
        auto ptr = &*b;
        return rstd::lexicographical_compare_three_way(
            a.vec.begin(), a.vec.end(), ptr, ptr + b.len());
    }
    friend bool operator==(char const* b, const String& a) noexcept {
        return rstd::lexicographical_compare_three_way(
                   a.vec.begin(), a.vec.end(), b, b + rstd::strlen(b)) ==
               rstd::strong_ordering::equal;
    }

    /// Returns a raw pointer to the underlying byte buffer.
    /// \return A const pointer to the first byte.
    constexpr auto as_raw_ptr() const noexcept -> const u8* { return vec.begin(); }

    /// Returns a const iterator to the beginning of the string.
    constexpr auto begin() const noexcept -> const char* { return rstd::bit_cast<const char*>(vec.begin()); }
    /// Returns a const iterator to the end of the string.
    constexpr auto end() const noexcept -> const char* { return rstd::bit_cast<const char*>(vec.end()); }
    /// Returns a pointer to the string data as a char array.
    /// \return A const `char*` pointer to the data.
    constexpr auto data() const noexcept -> const char* {
        return rstd::bit_cast<const char*>(vec.as_ptr().as_raw_ptr());
    }
    /// Returns the length of the string in bytes.
    /// \return The number of bytes in the string.
    constexpr auto size() const noexcept -> usize { return vec.len(); }
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
struct Impl<fmt::Write, String> : ImplBase<String> {
    auto write_str(const u8* p, usize len) -> bool {
        auto& self = this->self();
        for (usize i = 0; i < len; ++i) {
            self.push_back(p[i]);
        }
        return true;
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

template<mtp::is_int T>
struct Impl<fmt::Display, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& f) const -> bool {
        char  buf[64];
        char* p   = buf + 64;
        auto  val = this->self();
        if (val == 0) {
            return f.write_raw((const u8*)"0", 1);
        }
        u128 uval;
        bool              neg = false;
        if constexpr (mtp::same_as<T, i8> || mtp::same_as<T, i16> || mtp::same_as<T, i32> ||
                      mtp::same_as<T, i64> || mtp::same_as<T, i128> || mtp::same_as<T, int> ||
                      mtp::same_as<T, long> || mtp::same_as<T, long long>) {
            if (val < 0) {
                neg  = true;
                uval = (val == numeric_limits<T>::min())
                           ? (u128)numeric_limits<T>::max() + 1
                           : (u128)-val;
            } else {
                uval = (u128)val;
            }
        } else {
            uval = (u128)val;
        }

        while (uval > 0) {
            *--p = (char)('0' + (uval % 10));
            uval /= 10;
        }

        if (neg) *--p = '-';
        return f.write_raw((const u8*)p, (buf + 64) - p);
    }
};

template<mtp::is_int T>
struct Impl<fmt::Debug, T> : ImplBase<T> {
    auto fmt(fmt::Formatter& f) const -> bool { return as<fmt::Display>(this->self()).fmt(f); }
};

template<>
struct Impl<fmt::Display, char const*> : ImplBase<char const*> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto s = this->self();
        return f.write_raw((const u8*)s, rstd::strlen(s));
    }
};

template<>
struct Impl<fmt::Debug, char const*> : ImplBase<char const*> {
    auto fmt(fmt::Formatter& f) const -> bool {
        f.write_raw((const u8*)"\"", 1);
        as<fmt::Display>(this->self()).fmt(f);
        return f.write_raw((const u8*)"\"", 1);
    }
};

template<usize N>
struct Impl<fmt::Display, char[N]> : ImplBase<char[N]> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return f.write_raw((const u8*)this->self(), rstd::strlen(this->self()));
    }
};

template<usize N>
struct Impl<fmt::Display, char const[N]> : ImplBase<char const[N]> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return f.write_raw((const u8*)this->self(), rstd::strlen(this->self()));
    }
};

// Duration has no Display (matching Rust: format is ambiguous).
// Debug format matches Rust: "1.5s", "500ms", "1.234µs", "789ns".
template<>
struct Impl<fmt::Debug, time::Duration> : ImplBase<time::Duration> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto&     d     = this->self();
        const u64 secs  = d.as_secs();
        const u32 nanos = d.subsec_nanos();
        if (secs > 0) {
            // Render as seconds with up to 9 decimal places, trimming trailing zeros.
            auto s = rstd::format("{}", secs);
            f.write_raw(s.as_raw_ptr(), s.size());
            if (nanos != 0) {
                // Produce 9-digit fractional part then strip trailing zeros.
                char frac[10];
                u32  n = nanos;
                for (int i = 8; i >= 0; --i) {
                    frac[i] = char('0' + n % 10);
                    n /= 10;
                }
                frac[9] = '\0';
                int len = 9;
                while (len > 1 && frac[len - 1] == '0') --len;
                f.write_raw((const u8*)".", 1);
                f.write_raw((const u8*)frac, usize(len));
            }
            return f.write_raw((const u8*)"s", 1);
        } else if (nanos >= time::NANOS_PER_MILLI) {
            // milliseconds
            u32  ms  = nanos / time::NANOS_PER_MILLI;
            u32  rem = nanos % time::NANOS_PER_MILLI;
            auto s   = rstd::format("{}", ms);
            f.write_raw(s.as_raw_ptr(), s.size());
            if (rem != 0) {
                char frac[7];
                u32  r = rem;
                for (int i = 5; i >= 0; --i) {
                    frac[i] = char('0' + r % 10);
                    r /= 10;
                }
                frac[6] = '\0';
                int len = 6;
                while (len > 1 && frac[len - 1] == '0') --len;
                f.write_raw((const u8*)".", 1);
                f.write_raw((const u8*)frac, usize(len));
            }
            return f.write_raw((const u8*)"ms", 2);
        } else if (nanos >= time::NANOS_PER_MICRO) {
            // microseconds — use ASCII "us" (µ is multi-byte, avoid encoding issues)
            u32  us  = nanos / time::NANOS_PER_MICRO;
            u32  rem = nanos % time::NANOS_PER_MICRO;
            auto s   = rstd::format("{}", us);
            f.write_raw(s.as_raw_ptr(), s.size());
            if (rem != 0) {
                char frac[4];
                u32  r = rem;
                for (int i = 2; i >= 0; --i) {
                    frac[i] = char('0' + r % 10);
                    r /= 10;
                }
                frac[3] = '\0';
                int len = 3;
                while (len > 1 && frac[len - 1] == '0') --len;
                f.write_raw((const u8*)".", 1);
                f.write_raw((const u8*)frac, usize(len));
            }
            return f.write_raw((const u8*)"us", 2);
        } else {
            auto s = rstd::format("{}", nanos);
            f.write_raw(s.as_raw_ptr(), s.size());
            return f.write_raw((const u8*)"ns", 2);
        }
    }
};

template<mtp::same_as<ToString> T, Impled<fmt::Display> A>
struct Impl<T, A> : ImplBase<A> {
    auto to_string() const -> String { return rstd::format("{}", this->self()); }
};

template<mtp::same_as<cmp::PartialEq<String>> T, mtp::same_as<String> A>
struct Impl<T, A> : ImplBase<default_tag<A>> {
    auto eq(const String& other) const noexcept -> bool {
        return this->self().size() == other.size() &&
               rstd::mem::memcmp(this->self().begin(), other.begin(), this->self().size()) == 0;
    }
};

template<mtp::same_as<cmp::PartialEq<char const*>> T, mtp::same_as<String> A>
struct Impl<T, A> : ImplBase<default_tag<A>> {
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

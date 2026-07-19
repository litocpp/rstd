module;
#include <rstd/macro.hpp>
export module rstd:ffi.os_str;
export import :io;
export import rstd.alloc;

using ::alloc::vec::Vec;
using ::alloc::string::String;
using namespace rstd::prelude;

namespace rstd::ffi
{

/// An unsized, platform-native string type.
///
/// On Unix this is an arbitrary byte sequence (often UTF-8).
/// On Windows this would be WTF-8 (not yet implemented).
export struct OsStr {
    ~OsStr() = delete;
};

} // namespace rstd::ffi

namespace rstd
{

template<>
struct Impl<Sized, ffi::OsStr> {
    ~Impl() = delete;
};

template<>
struct Impl<ptr_::Pointee, ffi::OsStr> {
    using Metadata = usize;
};

/// A borrowed reference to a platform-native string.
template<>
struct ref<ffi::OsStr> : ref_base<ref<ffi::OsStr>, rstd::uint8_t[], false> {
    USE_TRAIT(ref)

    using Target = ffi::OsStr;

    rstd::uint8_t const* p { nullptr };
    usize                length {};

    constexpr ref() noexcept = default;
    constexpr ref(rstd::uint8_t const* p [[clang::lifetimebound]], usize len) noexcept
        : p(p), length(len) {}
    ref(u8 const* p [[clang::lifetimebound]]
        ,
        usize len) noexcept
        : p(rstd::as_bytes(slice<u8>::from_raw_parts(p, len)).as_raw_ptr()), length(len) {}

    /// Construct from a `ref<str>` (UTF-8 is always valid OS bytes).
    constexpr ref(ref<str> s [[clang::lifetimebound]]) noexcept: p(s.data()), length(s.size()) {}

    /// Construct from a null-terminated C string.
    ref(const char* c_str [[clang::lifetimebound]]
        ) noexcept
        : p(reinterpret_cast<rstd::uint8_t const*>(c_str)), length(rstd::strlen(c_str)) {}

    static constexpr auto from_raw_parts(rstd::uint8_t const* p [[clang::lifetimebound]],
                                         usize                len) noexcept -> Self {
        return { p, len };
    }
    static auto from_raw_parts(u8 const* p [[clang::lifetimebound]], usize len) noexcept -> Self {
        return { p, len };
    }

    /// Returns the encoded bytes of this OS string.
    constexpr auto as_encoded_bytes() const noexcept [[clang::lifetimebound]] -> slice<byte> {
        if (length == usize()) return {};
        return slice<byte>::from_raw_parts(p, length);
    }

    /// Attempts to convert to a UTF-8 string slice.
    ///
    /// Returns `None` if the bytes are not valid UTF-8.
    constexpr auto to_str() const noexcept -> Option<ref<str>> {
        if (char_::is_valid_utf8(p, length)) {
            ref<str> r(p, length);
            return Some(rstd::move(r));
        }
        return None();
    }

    /// Converts to a `String`, replacing invalid UTF-8 with U+FFFD.
    auto to_string_lossy() const -> String {
        auto         buf   = String::make();
        rstd::size_t index = 0;
        while (index < length.to_primitive()) {
            auto [cp, n] = char_::decode_utf8(p + index, usize(length.to_primitive() - index));
            if (cp == char_::REPLACEMENT && n == usize(1) && p[index] > 0x7F) {
                // Invalid byte — emit replacement character
                buf.push(char_::REPLACEMENT);
            } else {
                buf.push(cp);
            }
            index += n.to_primitive();
        }
        return buf;
    }

    constexpr auto len() const noexcept -> usize { return length; }
    constexpr auto is_empty() const noexcept -> bool { return length == usize {}; }
    constexpr auto data() const noexcept -> rstd::uint8_t const* { return p; }

    constexpr auto starts_with(ref<ffi::OsStr> prefix) const noexcept -> bool {
        if (prefix.len() > length) return false;
        for (rstd::size_t i = 0; i < prefix.len().to_primitive(); ++i) {
            if (p[i] != prefix.data()[i]) return false;
        }
        return true;
    }

    constexpr auto strip_prefix(ref<ffi::OsStr> prefix) const noexcept -> Option<ref<ffi::OsStr>> {
        if (! starts_with(prefix)) return None();
        return Some(ref<ffi::OsStr>::from_raw_parts(p + prefix.len().to_primitive(),
                                                    length - prefix.len()));
    }

    constexpr auto split_once(u8 delimiter) const noexcept
        -> Option<tuple<ref<ffi::OsStr>, ref<ffi::OsStr>>> {
        for (rstd::size_t i = 0; i < length.to_primitive(); ++i) {
            if (p[i] == delimiter.to_primitive()) {
                return Some(tuple<ref<ffi::OsStr>, ref<ffi::OsStr>>(
                    ref<ffi::OsStr>::from_raw_parts(p, usize(i)),
                    ref<ffi::OsStr>::from_raw_parts(p + i + 1,
                                                    usize(length.to_primitive() - i - 1))));
            }
        }
        return None();
    }

    constexpr operator bool() const { return length != usize {} && p != nullptr; }

    constexpr auto deref() const noexcept -> ref<Target> { return *this; }
};

} // namespace rstd

export namespace rstd::ffi
{

/// An owned, platform-native string.
///
/// On Unix this wraps `Vec<u8>`. Analogous to Rust's `OsString`.
class OsString {
    Vec<u8> inner;

    explicit OsString(Vec<u8>&& v): inner(rstd::move(v)) {}

public:
    OsString()                               = default;
    OsString(OsString&&) noexcept            = default;
    OsString& operator=(OsString&&) noexcept = default;

    /// Creates an empty `OsString`.
    static auto make() -> OsString { return {}; }

    /// Creates an `OsString` from a `String` (zero-cost move on Unix).
    static auto from(String&& s) -> OsString {
        auto bytes = rstd::as<Into<Vec<u8>>>(s).into();
        return OsString { rstd::move(bytes) };
    }

    /// Creates an `OsString` by copying a `ref<str>`.
    static auto from(ref<str> s) -> OsString { return from(String::make(s)); }

    /// Creates an `OsString` by copying a `ref<OsStr>`.
    static auto from(ref<OsStr> s) -> OsString {
        return OsString { Vec<u8>::copy_from_bytes(s.as_encoded_bytes()) };
    }

    /// Creates an `OsString` from raw bytes without validation.
    static auto from_encoded_bytes_unchecked(Vec<u8>&& bytes) -> OsString {
        return OsString { rstd::move(bytes) };
    }

    /// Returns a borrowed `ref<OsStr>`.
    auto as_os_str() const noexcept [[clang::lifetimebound]] -> ref<OsStr> {
        return ref<OsStr>::from_raw_parts(inner.begin(), inner.len());
    }

    /// Attempts to convert to a `String`.
    ///
    /// Returns `Err(self)` if the bytes are not valid UTF-8.
    auto into_string() -> result::Result<String, OsString> {
        if (char_::is_valid_utf8(inner.begin(), inner.len())) {
            return Ok(String::from_utf8_unchecked(rstd::move(inner)));
        }
        return Err(OsString { rstd::move(inner) });
    }

    /// Appends an `OsStr` to this string.
    void push(ref<OsStr> s) { inner.extend_from_bytes(s.as_encoded_bytes()); }

    auto len() const noexcept -> usize { return inner.len(); }
    auto is_empty() const noexcept -> bool { return inner.len() == usize {}; }
    auto capacity() const noexcept -> usize { return inner.capacity(); }
    void clear() { inner.clear(); }

    /// Implicit conversion to `ref<OsStr>`.
    operator ref<OsStr>() const noexcept [[clang::lifetimebound]] { return as_os_str(); }
};

} // namespace rstd::ffi

namespace rstd
{

template<>
struct Impl<convert::TryFrom<ffi::OsString>, ::alloc::string::String> {
    using Error = ffi::OsString;

    static auto try_from(ffi::OsString value) -> Result<::alloc::string::String, Error> {
        return value.into_string();
    }
};

} // namespace rstd

// ── Display for ref<OsStr> ───────────────────────────────────────────────
namespace rstd
{

template<>
struct Impl<fmt::Display, ref<ffi::OsStr>> : ImplBase<ref<ffi::OsStr>> {
    auto fmt(fmt::Formatter& f) const -> bool {
        // Print as UTF-8 lossy — valid bytes pass through, invalid → replacement
        auto&        s     = this->self();
        rstd::size_t index = 0;
        while (index < s.len().to_primitive()) {
            auto [cp, n] =
                char_::decode_utf8(s.data() + index, usize(s.len().to_primitive() - index));
            rstd::uint8_t buf[4];
            auto          wrote = char_::encode_utf8(cp, buf);
            if (! f.write_raw(buf, wrote.to_primitive())) return false;
            index += n.to_primitive();
        }
        return true;
    }
};

template<>
struct Impl<fmt::Debug, ref<ffi::OsStr>> : ImplBase<ref<ffi::OsStr>> {
    auto fmt(fmt::Formatter& f) const -> bool {
        f.write_raw("\"", 1);
        as<fmt::Display>(this->self()).fmt(f);
        return f.write_raw("\"", 1);
    }
};

} // namespace rstd

module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#error "rstd::ffi::OsStr requires a Windows platform encoding owner"
#endif
export module rstd:ffi.os_str;
export import :io;
export import rstd.alloc;

using ::alloc::vec::Vec;
using ::alloc::string::String;
using namespace rstd::prelude;

namespace rstd::ffi
{

namespace os_string_platform
{

struct Slice {
    byte const* data {};
    usize       length {};

    static constexpr auto from_encoded_bytes_unchecked(slice<u8> bytes) noexcept -> Slice {
        return { bytes.as_raw_ptr(), bytes.len() };
    }

    constexpr auto as_encoded_bytes() const noexcept -> slice<u8> {
        return slice<u8>::from_raw_parts(data, length);
    }
};

class Buf {
    Vec<u8> inner_;

public:
    Buf() = default;
    explicit Buf(Vec<u8>&& inner): inner_(rstd::move(inner)) {}

    static auto from_encoded_bytes_unchecked(Vec<u8>&& bytes) -> Buf {
        return Buf(rstd::move(bytes));
    }

    auto as_slice() const noexcept [[clang::lifetimebound]] -> Slice {
        return Slice::from_encoded_bytes_unchecked(inner_.as_slice());
    }

    auto into_inner() && -> Vec<u8> { return rstd::move(inner_); }
    auto inner() noexcept -> Vec<u8>& { return inner_; }
    auto inner() const noexcept -> Vec<u8> const& { return inner_; }
};

} // namespace os_string_platform

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
struct ref<ffi::OsStr> : ref_base<ref<ffi::OsStr>, byte[], false> {
    USE_TRAIT(ref)

    byte const* p { nullptr };
    usize       length {};

    constexpr ref() noexcept = default;
    constexpr ref(ffi::os_string_platform::Slice value [[clang::lifetimebound]]) noexcept
        : p(value.data), length(value.length) {}

    /// Construct from a `ref<str>` (UTF-8 is always valid OS bytes).
    constexpr ref(ref<str> s [[clang::lifetimebound]]) noexcept: p(s.data()), length(s.size()) {}

    static constexpr auto from_encoded_bytes_unchecked(slice<u8> bytes
                                                       [[clang::lifetimebound]]) noexcept -> Self {
        return { ffi::os_string_platform::Slice::from_encoded_bytes_unchecked(bytes) };
    }

    /// Returns the encoded bytes of this OS string.
    constexpr auto as_encoded_bytes() const noexcept [[clang::lifetimebound]] -> slice<u8> {
        return slice<u8>::from_raw_parts(p, length);
    }

    /// Attempts to convert to a UTF-8 string slice.
    ///
    /// Returns `None` if the bytes are not valid UTF-8.
    constexpr auto to_str() const noexcept -> Option<ref<str>> {
        auto result = str_::from_utf8(as_encoded_bytes());
        if (result.is_err()) return None();
        return Some(rstd::move(result).unwrap_unchecked());
    }

    /// Converts to a `String`, replacing invalid UTF-8 with U+FFFD.
    auto to_string_lossy() const -> String {
        auto         buf   = String::make();
        rstd::size_t index = 0;
        while (index < length.to_primitive()) {
            auto [cp, n] = char_::decode_utf8(p + index, usize(length.to_primitive() - index));
            if (cp == char_::REPLACEMENT && n == usize(1) &&
                u8::from_byte(p[index]).to_primitive() > 0x7F) {
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
    constexpr auto starts_with(ref<ffi::OsStr> prefix) const noexcept -> bool {
        if (prefix.len() > length) return false;
        for (rstd::size_t i = 0; i < prefix.len().to_primitive(); ++i) {
            if (p[i] != prefix.p[i]) return false;
        }
        return true;
    }

    constexpr auto strip_prefix(ref<ffi::OsStr> prefix) const noexcept -> Option<ref<ffi::OsStr>> {
        if (! starts_with(prefix)) return None();
        return Some(ref<ffi::OsStr>(p + prefix.len().to_primitive(), length - prefix.len()));
    }

    constexpr auto split_once(u8 delimiter) const noexcept
        -> Option<tuple<ref<ffi::OsStr>, ref<ffi::OsStr>>> {
        for (rstd::size_t i = 0; i < length.to_primitive(); ++i) {
            if (u8::from_byte(p[i]) == delimiter) {
                return Some(tuple<ref<ffi::OsStr>, ref<ffi::OsStr>>(
                    ref<ffi::OsStr>(p, usize(i)),
                    ref<ffi::OsStr>(p + i + 1, usize(length.to_primitive() - i - 1))));
            }
        }
        return None();
    }

    constexpr operator bool() const { return length != usize {} && p != nullptr; }

    constexpr auto deref() const noexcept -> ref<ffi::OsStr> { return *this; }

private:
    constexpr ref(byte const* data [[clang::lifetimebound]], usize len) noexcept
        : p(data), length(len) {}
};

} // namespace rstd

export namespace rstd::ffi
{

/// An owned, platform-native string.
///
/// On Unix this wraps `Vec<u8>`. Analogous to Rust's `OsString`.
class OsString {
    os_string_platform::Buf inner;

    explicit OsString(os_string_platform::Buf&& value): inner(rstd::move(value)) {}

public:
    OsString()                               = default;
    OsString(OsString&&) noexcept            = default;
    OsString& operator=(OsString&&) noexcept = default;

    /// Creates an empty `OsString`.
    static auto make() -> OsString { return {}; }

    /// Creates an `OsString` from a `String` (zero-cost move on Unix).
    static auto from(String&& s) -> OsString {
        auto bytes = rstd::as<Into<Vec<u8>>>(s).into();
        return from_encoded_bytes_unchecked(rstd::move(bytes));
    }

    /// Creates an `OsString` by copying a `ref<str>`.
    static auto from(ref<str> s) -> OsString { return from(String::make(s)); }

    /// Creates an `OsString` by copying a `ref<OsStr>`.
    static auto from(ref<OsStr> s) -> OsString {
        return OsString { os_string_platform::Buf(Vec<u8>::from(s.as_encoded_bytes())) };
    }

    /// Creates an `OsString` from raw bytes without validation.
    static auto from_encoded_bytes_unchecked(Vec<u8>&& bytes) -> OsString {
        return OsString { os_string_platform::Buf::from_encoded_bytes_unchecked(
            rstd::move(bytes)) };
    }

    /// Returns a borrowed `ref<OsStr>`.
    auto as_os_str() const noexcept [[clang::lifetimebound]] -> ref<OsStr> {
        return ref<OsStr> { inner.as_slice() };
    }

    /// Attempts to convert to a `String`.
    ///
    /// Returns `Err(self)` if the bytes are not valid UTF-8.
    auto into_string() -> result::Result<String, OsString> {
        auto bytes = rstd::move(inner).into_inner();
        if (str_::validate_utf8(bytes.as_slice()).is_ok()) {
            return Ok(String::from_utf8_unchecked(rstd::move(bytes)));
        }
        return Err(from_encoded_bytes_unchecked(rstd::move(bytes)));
    }

    /// Appends an `OsStr` to this string.
    void push(ref<OsStr> s) { inner.inner().extend_from_slice(s.as_encoded_bytes()); }

    auto len() const noexcept -> usize { return inner.inner().len(); }
    auto is_empty() const noexcept -> bool { return inner.inner().is_empty(); }
    auto capacity() const noexcept -> usize { return inner.inner().capacity(); }
    void clear() { inner.inner().clear(); }

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
        auto         bytes = s.as_encoded_bytes();
        rstd::size_t index = 0;
        while (index < s.len().to_primitive()) {
            auto [cp, n] = char_::decode_utf8(bytes.as_raw_ptr() + index,
                                              usize(s.len().to_primitive() - index));
            byte buf[4];
            auto wrote = char_::encode_utf8(cp, buf);
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

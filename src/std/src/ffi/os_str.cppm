module;
#include <rstd/macro.hpp>
export module rstd:ffi.os_str;
export import :io;
export import rstd.alloc;
import :ffi.os_str.platform;
import :ffi.os_str.encoding;

using ::alloc::vec::Vec;
using ::alloc::string::String;
using namespace rstd::prelude;

namespace rstd::ffi
{

/// An unsized, platform-native string type.
///
/// On Unix this is an arbitrary byte sequence (often UTF-8).
/// On Windows this stores WTF-8, preserving unpaired UTF-16 surrogates.
export struct OsStr {
    ~OsStr() = delete;
};

export class OsString;
export struct Display;

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
        auto buf   = String::make();
        auto index = usize();
        while (index < length) {
            buf.push(ffi::os_encoding::decode_lossy(
                as_encoded_bytes(), index, ffi::os_string_platform::USE_WTF8));
        }
        return buf;
    }

    constexpr auto len() const noexcept -> usize { return length; }
    auto           to_os_string() const -> ffi::OsString;
    auto           display() const noexcept [[clang::lifetimebound]] -> ffi::Display;
    auto           to_ascii_lowercase() const -> ffi::OsString;
    auto           to_ascii_uppercase() const -> ffi::OsString;
    constexpr auto is_ascii() const noexcept -> bool {
        for (auto value : as_encoded_bytes())
            if (value > u8(127)) return false;
        return true;
    }
    constexpr auto eq_ignore_ascii_case(ref<ffi::OsStr> other) const noexcept -> bool {
        if (length != other.length) return false;
        const auto lower = [](unsigned value) {
            return value >= 'A' && value <= 'Z' ? value + 32 : value;
        };
        for (auto index = usize(); index < length; ++index)
            if (lower(as_encoded_bytes()[index].to_primitive()) !=
                lower(other.as_encoded_bytes()[index].to_primitive()))
                return false;
        return true;
    }
    constexpr auto operator==(ref<ffi::OsStr> other) const noexcept -> bool {
        return as_encoded_bytes() == other.as_encoded_bytes();
    }
    constexpr auto operator<=>(ref<ffi::OsStr> other) const noexcept {
        auto common = length < other.length ? length : other.length;
        for (auto index = usize(); index < common; ++index) {
            auto order = as_encoded_bytes()[index] <=> other.as_encoded_bytes()[index];
            if (order != 0) return order;
        }
        return length <=> other.length;
    }
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
        if (delimiter > u8(127)) rstd::panic { "OsStr::split_once requires an ASCII delimiter" };
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

struct Display {
    ref<OsStr> value;
};

/// An owned, platform-native string.
///
/// On Unix this wraps `Vec<u8>`. Analogous to Rust's `OsString`.
/// On Windows this uses WTF-8 encoded storage.
class OsString {
    os_string_platform::Buf inner;

    explicit OsString(os_string_platform::Buf&& value): inner(rstd::move(value)) {}

public:
    USE_TRAIT(OsString)

    OsString()                               = default;
    OsString(OsString&&) noexcept            = default;
    OsString& operator=(OsString&&) noexcept = default;

    /// Creates an empty `OsString`.
    static auto make() -> OsString { return {}; }
    static auto with_capacity(usize capacity) -> OsString {
        return from_encoded_bytes_unchecked(Vec<u8>::with_capacity(capacity));
    }

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

    auto clone() const -> OsString { return from(as_os_str()); }

    void clone_from(const OsString& source) { *this = source.clone(); }

    /// Bytes must use this platform's encoding; Windows requires valid WTF-8.
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

    auto into_encoded_bytes() && -> Vec<u8> { return rstd::move(inner).into_inner(); }

    /// Appends an `OsStr` to this string.
    void push(ref<OsStr> s) { inner.push(s.as_encoded_bytes()); }

    auto len() const noexcept -> usize { return inner.len(); }
    auto is_empty() const noexcept -> bool { return len() == usize(); }
    auto capacity() const noexcept -> usize { return inner.capacity(); }
    void clear() { inner.clear(); }
    void reserve(usize additional) { inner.reserve(additional); }
    void reserve_exact(usize additional) { inner.reserve_exact(additional); }
    auto try_reserve(usize additional) { return inner.try_reserve(additional); }
    auto try_reserve_exact(usize additional) { return inner.try_reserve_exact(additional); }
    void shrink_to_fit() { inner.shrink_to_fit(); }
    void shrink_to(usize capacity) { inner.shrink_to(capacity); }
    void truncate(usize length) { inner.truncate(length); }
    void make_ascii_lowercase() { inner.ascii_case(false); }
    void make_ascii_uppercase() { inner.ascii_case(true); }
    auto is_ascii() const noexcept -> bool { return as_os_str().is_ascii(); }
    auto eq_ignore_ascii_case(ref<OsStr> other) const noexcept -> bool {
        return as_os_str().eq_ignore_ascii_case(other);
    }
    auto operator==(const OsString& other) const noexcept -> bool {
        return as_os_str() == other.as_os_str();
    }
    auto operator==(ref<OsStr> other) const noexcept -> bool { return as_os_str() == other; }
    auto operator<=>(const OsString& other) const noexcept {
        return as_os_str() <=> other.as_os_str();
    }

    /// Implicit conversion to `ref<OsStr>`.
    operator ref<OsStr>() const noexcept [[clang::lifetimebound]] { return as_os_str(); }
};

} // namespace rstd::ffi

namespace rstd
{

inline auto ref<ffi::OsStr>::to_os_string() const -> ffi::OsString {
    return ffi::OsString::from(*this);
}
inline auto ref<ffi::OsStr>::display() const noexcept -> ffi::Display {
    return { *this };
}
inline auto ref<ffi::OsStr>::to_ascii_lowercase() const -> ffi::OsString {
    auto result = to_os_string();
    result.make_ascii_lowercase();
    return result;
}
inline auto ref<ffi::OsStr>::to_ascii_uppercase() const -> ffi::OsString {
    auto result = to_os_string();
    result.make_ascii_uppercase();
    return result;
}

template<>
struct Impl<convert::TryFrom<ffi::OsString>, ::alloc::string::String> {
    using Error = ffi::OsString;

    static auto try_from(ffi::OsString value) -> Result<::alloc::string::String, Error> {
        return value.into_string();
    }
};

} // namespace rstd

namespace rstd
{

template<>
struct Impl<fmt::Display, ffi::Display> : ImplBase<ffi::Display> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto bytes = this->self().value.as_encoded_bytes();
        auto index = usize();
        while (index < bytes.len()) {
            auto cp =
                ffi::os_encoding::decode_lossy(bytes, index, ffi::os_string_platform::USE_WTF8);
            byte buf[4];
            auto wrote = char_::encode_utf8(cp, buf);
            if (! f.write_raw(buf, wrote.to_primitive())) return false;
        }
        return true;
    }
};

template<>
struct Impl<fmt::Debug, ref<ffi::OsStr>> : ImplBase<ref<ffi::OsStr>> {
    auto fmt(fmt::Formatter& f) const -> bool {
        f.write_raw("\"", 1);
        auto display = this->self().display();
        if (! as<fmt::Display>(display).fmt(f)) return false;
        return f.write_raw("\"", 1);
    }
};

template<>
struct Impl<hash::Hash, ref<ffi::OsStr>> : ImplBase<ref<ffi::OsStr>> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().len(), state);
        as<hash::Hasher>(state).write(this->self().as_encoded_bytes());
    }
};

template<>
struct Impl<hash::Hash, ffi::OsString> : ImplBase<ffi::OsString> {
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::hash_into(this->self().as_os_str(), state);
    }
};

} // namespace rstd

export module rstd:path;
export import :ffi;
export import rstd.alloc;

using ::alloc::string::String;
using ::alloc::vec::Vec;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;
using namespace rstd::prelude;

namespace rstd::path
{

/// An unsized path type, analogous to Rust's `std::path::Path`.
///
/// Internally an `OsStr`. This is a borrowed, unsized type — use
/// `ref<Path>` for references and `PathBuf` for owned paths.
export struct Path {
    ~Path() = delete;
};

} // namespace rstd::path

namespace rstd
{

template<>
struct Impl<Sized, path::Path> { ~Impl() = delete; };

template<>
struct Impl<ptr_::Pointee, path::Path> { using Metadata = usize; };

#if defined(RSTD_OS_WINDOWS)
inline constexpr u8 PATH_SEP = '\\';
inline constexpr bool HAS_DRIVE_PREFIX = true;
#else
inline constexpr u8 PATH_SEP = '/';
inline constexpr bool HAS_DRIVE_PREFIX = false;
#endif

namespace path_detail
{

constexpr bool is_sep(u8 c) {
#if defined(RSTD_OS_WINDOWS)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

/// Find the start of the file name component (after the last separator).
constexpr auto file_name_start(u8 const* p, usize len) -> usize {
    if (len == 0) return 0;
    usize i = len;
    // Skip trailing separators
    while (i > 0 && is_sep(p[i - 1])) --i;
    if (i == 0) return len; // all separators → no file name
    usize end = i;
    while (i > 0 && !is_sep(p[i - 1])) --i;
    (void)end;
    return i;
}

} // namespace path_detail

/// A borrowed reference to a filesystem path.
template<>
struct ref<path::Path> : ref_base<ref<path::Path>, u8[], false> {
    u8 const* p { nullptr };
    usize     length { 0 };

    using Self = ref;
    constexpr ref() noexcept = default;
    constexpr ref(u8 const* p, usize len) noexcept : p(p), length(len) {}

    /// Construct from a `ref<OsStr>`.
    constexpr ref(ref<OsStr> s) noexcept : p(s.data()), length(s.len()) {}

    /// Construct from a `ref<str>`.
    constexpr ref(ref<str> s) noexcept : p(s.data()), length(s.size()) {}

    /// Construct from a null-terminated C string.
    constexpr ref(const char* c) noexcept
        : p(rstd::bit_cast<u8 const*>(c)), length(rstd::strlen(c)) {}

    static constexpr auto from_raw_parts(u8 const* p, usize len) noexcept -> Self {
        return { p, len };
    }

    /// Returns the underlying `OsStr`.
    constexpr auto as_os_str() const noexcept -> ref<OsStr> {
        return ref<OsStr>::from_raw_parts(p, length);
    }

    /// Attempts to yield a `ref<str>` if the path is valid UTF-8.
    constexpr auto to_str() const noexcept -> Option<ref<str>> {
        return as_os_str().to_str();
    }

    /// Converts to a `String`, replacing invalid UTF-8 with U+FFFD.
    auto to_string_lossy() const -> String {
        return as_os_str().to_string_lossy();
    }

    /// Returns `true` if the path starts with a root separator.
    constexpr auto is_absolute() const noexcept -> bool {
        if (length == 0) return false;
#if defined(RSTD_OS_WINDOWS)
        // UNC or drive-letter absolute: \\... or C:\...
        if (length >= 2 && path_detail::is_sep(p[0]) && path_detail::is_sep(p[1])) return true;
        if (length >= 3 && p[1] == ':' && path_detail::is_sep(p[2])) return true;
        return false;
#else
        return p[0] == '/';
#endif
    }

    /// Returns `true` if the path is not absolute.
    constexpr auto is_relative() const noexcept -> bool { return !is_absolute(); }

    /// Returns the parent path (everything before the last component).
    ///
    /// Returns `None` for root or empty paths.
    constexpr auto parent() const noexcept -> Option<ref<path::Path>> {
        if (length == 0) return None();

        // Find where the file name starts
        auto i = path_detail::file_name_start(p, length);

        if (i == length) {
            // No file name (all separators, e.g. "/") → no parent
            return None();
        }
        if (i == 0) {
            // No separator found → single component, no parent
            return None();
        }

        // Strip trailing separators from parent, but keep at least one char
        // so "/" stays as "/" rather than becoming ""
        usize pi = i;
        while (pi > 1 && path_detail::is_sep(p[pi - 1])) --pi;
        ref<path::Path> r(p, pi);
        return Some(rstd::move(r));
    }

    /// Returns the final component of the path (file or directory name).
    constexpr auto file_name() const noexcept -> Option<ref<OsStr>> {
        if (length == 0) return None();
        usize start = path_detail::file_name_start(p, length);
        usize end = length;
        while (end > start && path_detail::is_sep(p[end - 1])) --end;
        if (end <= start) return None();
        ref<OsStr> r = ref<OsStr>::from_raw_parts(p + start, end - start);
        return Some(rstd::move(r));
    }

    /// Returns the extension of the file name (after the last `.`).
    constexpr auto extension() const noexcept -> Option<ref<OsStr>> {
        auto fn = file_name();
        if (fn.is_none()) return None();
        auto name = *fn;
        // Find last '.' that is not the first character
        usize dot = name.len();
        for (usize i = name.len(); i > 1; --i) {
            if (name.data()[i - 1] == '.') { dot = i - 1; break; }
        }
        if (dot == name.len() || dot == 0) return None();
        ref<OsStr> r = ref<OsStr>::from_raw_parts(name.data() + dot + 1, name.len() - dot - 1);
        return Some(rstd::move(r));
    }

    constexpr auto len() const noexcept -> usize { return length; }
    constexpr auto is_empty() const noexcept -> bool { return length == 0; }
    constexpr auto data() const noexcept -> u8 const* { return p; }

    constexpr operator bool() const { return length > 0 && p != nullptr; }
};

} // namespace rstd

export namespace rstd::path
{

/// An owned, mutable filesystem path, analogous to Rust's `PathBuf`.
class PathBuf {
    OsString inner;

    explicit PathBuf(OsString&& s) : inner(rstd::move(s)) {}

public:
    PathBuf() = default;
    PathBuf(PathBuf&&) noexcept = default;
    PathBuf& operator=(PathBuf&&) noexcept = default;

    /// Creates an empty `PathBuf`.
    static auto make() -> PathBuf { return {}; }

    /// Creates a `PathBuf` from a `String`.
    static auto from(String&& s) -> PathBuf {
        return PathBuf { OsString::from(rstd::move(s)) };
    }

    /// Creates a `PathBuf` from a `ref<str>`.
    static auto from(ref<str> s) -> PathBuf {
        return PathBuf { OsString::from(s) };
    }

    /// Creates a `PathBuf` from an `OsString`.
    static auto from(OsString&& s) -> PathBuf {
        return PathBuf { rstd::move(s) };
    }

    /// Creates a `PathBuf` from a C string.
    static auto from(const char* s) -> PathBuf {
        return from(ref<str>(s));
    }

    /// Returns a borrowed `ref<Path>`.
    auto as_path() const noexcept -> ref<Path> {
        return ref<Path>(inner.as_os_str());
    }

    /// Consumes the `PathBuf` and returns the inner `OsString`.
    auto into_os_string() -> OsString { return rstd::move(inner); }

    /// Extends the path with `component`.
    ///
    /// If `component` is absolute, it replaces the current path.
    /// Otherwise, a separator is inserted if needed and `component` is appended.
    void push(ref<Path> component) {
        auto comp = component.as_os_str();
        ref<Path> comp_path(comp);
        if (comp_path.is_absolute()) {
            inner = OsString::from(comp);
            return;
        }
        // Add separator if current path is non-empty and doesn't end with one
        if (inner.len() > 0) {
            auto bytes = inner.as_os_str().as_encoded_bytes();
            if (!path_detail::is_sep((&*bytes)[bytes.len() - 1])) {
                u8 sep = PATH_SEP;
                inner.push(ref<OsStr>::from_raw_parts(&sep, 1));
            }
        }
        inner.push(comp);
    }

    /// Removes the last component. Returns `false` if already at root/empty.
    auto pop() -> bool {
        auto p = as_path().parent();
        if (p.is_none()) return false;
        auto parent = *p;
        inner = OsString::from(parent.as_os_str());
        return true;
    }

    /// Creates a new `PathBuf` by joining this path with a component.
    auto join(ref<Path> component) const -> PathBuf {
        auto result = PathBuf::from(OsString::from(inner.as_os_str()));
        result.push(component);
        return result;
    }

    auto len() const noexcept -> usize { return inner.len(); }
    auto is_empty() const noexcept -> bool { return inner.is_empty(); }

    /// Implicit conversion to `ref<Path>`.
    operator ref<Path>() const noexcept { return as_path(); }
};

} // namespace rstd::path

// ── Display for ref<Path> ────────────────────────────────────────────────
namespace rstd
{

template<>
struct Impl<fmt::Display, ref<path::Path>> : ImplBase<ref<path::Path>> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto& s = this->self();
        usize i = 0;
        while (i < s.len()) {
            auto [cp, n] = char_::decode_utf8(s.data() + i, s.len() - i);
            u8 buf[4];
            auto wrote = char_::encode_utf8(cp, buf);
            if (! f.write_raw(buf, wrote)) return false;
            i += n;
        }
        return true;
    }
};

template<>
struct Impl<fmt::Debug, ref<path::Path>> : ImplBase<ref<path::Path>> {
    auto fmt(fmt::Formatter& f) const -> bool {
        f.write_raw((const u8*)"\"", 1);
        as<fmt::Display>(this->self()).fmt(f);
        return f.write_raw((const u8*)"\"", 1);
    }
};

} // namespace rstd

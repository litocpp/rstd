module;
#include <rstd/macro.hpp>
export module rstd:path;
export import :ffi;
export import rstd.alloc;

using ::alloc::ffi::CString;
using ::alloc::ffi::NulError;
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

export enum class ComponentKind {
    RootDir,
    CurDir,
    ParentDir,
    Normal,
};

export class Component {
public:
    constexpr Component() noexcept = default;

    static constexpr auto root_dir() noexcept -> Component {
        return Component { ComponentKind::RootDir, {} };
    }
    static constexpr auto cur_dir() noexcept -> Component {
        return Component { ComponentKind::CurDir, {} };
    }
    static constexpr auto parent_dir() noexcept -> Component {
        return Component { ComponentKind::ParentDir, {} };
    }
    static constexpr auto normal(ref<OsStr> s [[clang::lifetimebound]]) noexcept -> Component {
        return Component { ComponentKind::Normal, s };
    }

    constexpr auto kind() const noexcept -> ComponentKind { return m_kind; }
    constexpr auto is_root_dir() const noexcept -> bool { return m_kind == ComponentKind::RootDir; }
    constexpr auto is_cur_dir() const noexcept -> bool { return m_kind == ComponentKind::CurDir; }
    constexpr auto is_parent_dir() const noexcept -> bool {
        return m_kind == ComponentKind::ParentDir;
    }
    constexpr auto is_normal() const noexcept -> bool { return m_kind == ComponentKind::Normal; }

    auto as_os_str() const noexcept -> ref<OsStr> {
        switch (m_kind) {
        case ComponentKind::RootDir: return ref<OsStr>("/");
        case ComponentKind::CurDir: return ref<OsStr>(".");
        case ComponentKind::ParentDir: return ref<OsStr>("..");
        case ComponentKind::Normal: return m_os;
        }
        return {};
    }

    constexpr auto operator==(const Component& other) const noexcept -> bool {
        if (m_kind != other.m_kind) return false;
        if (m_kind != ComponentKind::Normal) return true;
        if (m_os.len() != other.m_os.len()) return false;
        for (rstd::size_t i = 0; i < m_os.len().to_primitive(); ++i) {
            if (m_os.data()[i] != other.m_os.data()[i]) return false;
        }
        return true;
    }

private:
    constexpr Component(ComponentKind kind, ref<OsStr> os) noexcept: m_kind(kind), m_os(os) {}

    ComponentKind m_kind { ComponentKind::Normal };
    ref<OsStr>    m_os {};
};

export class Components {
public:
    using Item = Component;

    constexpr Components() noexcept = default;
    constexpr Components(rstd::uint8_t const* p [[clang::lifetimebound]], usize len) noexcept;

    constexpr auto next() noexcept -> Option<Component>;
    constexpr auto as_path() const noexcept -> ref<Path>;

private:
    constexpr auto remaining_start() const noexcept -> rstd::size_t;

    rstd::uint8_t const* m_path { nullptr };
    rstd::size_t         m_len {};
    rstd::size_t         m_pos {};
    bool                 m_root_pending { false };
    bool                 m_cur_pending { false };
};

export class PathBuf;

} // namespace rstd::path

namespace rstd
{

template<>
struct Impl<Sized, path::Path> {
    ~Impl() = delete;
};

template<>
struct Impl<ptr_::Pointee, path::Path> {
    using Metadata = usize;
};

#if defined(RSTD_OS_WINDOWS)
inline constexpr rstd::uint8_t PATH_SEP         = '\\';
inline constexpr bool          HAS_DRIVE_PREFIX = true;
#else
inline constexpr rstd::uint8_t PATH_SEP         = '/';
inline constexpr bool          HAS_DRIVE_PREFIX = false;
#endif

namespace path_detail
{

constexpr bool is_sep(rstd::uint8_t c) {
#if defined(RSTD_OS_WINDOWS)
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

constexpr auto root_len(rstd::uint8_t const* p, rstd::size_t len) -> rstd::size_t {
    if (len == 0) return 0;
#if defined(RSTD_OS_WINDOWS)
    if (len >= 3 && p[1] == ':' && is_sep(p[2])) return 3;
    if (is_sep(p[0])) return 1;
    return 0;
#else
    return p[0] == '/' ? 1 : 0;
#endif
}

constexpr auto skip_seps(rstd::uint8_t const* p, rstd::size_t len, rstd::size_t pos)
    -> rstd::size_t {
    while (pos < len && is_sep(p[pos])) ++pos;
    return pos;
}

constexpr auto eq_bytes(rstd::uint8_t const* p, rstd::size_t len, const char* s) -> bool {
    rstd::size_t i = 0;
    while (i < len && s[i] != '\0') {
        if (p[i] != static_cast<rstd::uint8_t>(s[i])) return false;
        ++i;
    }
    return i == len && s[i] == '\0';
}

constexpr auto include_cur_dir(rstd::uint8_t const* p, rstd::size_t len) -> bool {
    if (root_len(p, len) != 0) return false;
    if (len == 1) return p[0] == '.';
    return len > 1 && p[0] == '.' && is_sep(p[1]);
}

/// Find the start of the file name component (after the last separator).
constexpr auto file_name_start(rstd::uint8_t const* p, rstd::size_t len) -> rstd::size_t {
    if (len == 0) return 0;
    rstd::size_t i = len;
    // Skip trailing separators
    while (i > 0 && is_sep(p[i - 1])) --i;
    if (i == 0) return len; // all separators → no file name
    rstd::size_t end = i;
    while (i > 0 && ! is_sep(p[i - 1])) --i;
    (void)end;
    return i;
}

} // namespace path_detail

namespace path
{

constexpr Components::Components(rstd::uint8_t const* p, usize len) noexcept
    : m_path(p), m_len(len.to_primitive()) {
    auto root = path_detail::root_len(p, m_len);
    if (root != 0) {
        m_root_pending = true;
        m_pos          = root;
    } else if (path_detail::include_cur_dir(p, m_len)) {
        m_cur_pending = true;
        m_pos         = 1;
    }
}

constexpr auto Components::next() noexcept -> Option<Component> {
    if (m_root_pending) {
        m_root_pending = false;
        return Some(Component::root_dir());
    }
    if (m_cur_pending) {
        m_cur_pending = false;
        return Some(Component::cur_dir());
    }

    while (true) {
        m_pos = path_detail::skip_seps(m_path, m_len, m_pos);
        if (m_pos >= m_len) return None();

        auto start = m_pos;
        while (m_pos < m_len && ! path_detail::is_sep(m_path[m_pos])) ++m_pos;
        auto len = m_pos - start;

        if (path_detail::eq_bytes(m_path + start, len, ".")) continue;
        if (path_detail::eq_bytes(m_path + start, len, "..")) {
            return Some(Component::parent_dir());
        }
        return Some(Component::normal(ref<OsStr>::from_raw_parts(m_path + start, usize(len))));
    }
}

constexpr auto Components::remaining_start() const noexcept -> rstd::size_t {
    if (m_root_pending) return 0;
    if (m_cur_pending) return 0;
    return path_detail::skip_seps(m_path, m_len, m_pos);
}

} // namespace path

/// A borrowed reference to a filesystem path.
template<>
struct ref<path::Path> : ref_base<ref<path::Path>, rstd::uint8_t[], false> {
    USE_TRAIT(ref)

    using Target = path::Path;

    rstd::uint8_t const* p { nullptr };
    usize                length {};

    constexpr ref() noexcept = default;
    constexpr ref(rstd::uint8_t const* p [[clang::lifetimebound]], usize len) noexcept
        : p(p), length(len) {}
    ref(u8 const* p [[clang::lifetimebound]]
        ,
        usize len) noexcept
        : p(rstd::as_bytes(slice<u8>::from_raw_parts(p, len)).as_raw_ptr()), length(len) {}

    /// Construct from a `ref<OsStr>`.
    constexpr ref(ref<OsStr> s [[clang::lifetimebound]]) noexcept: p(s.data()), length(s.len()) {}

    /// Construct from a `ref<str>`.
    constexpr ref(ref<str> s [[clang::lifetimebound]]) noexcept: p(s.data()), length(s.size()) {}

    /// Construct from a null-terminated C string.
    ref(const char* c [[clang::lifetimebound]]
        ) noexcept
        : p(reinterpret_cast<rstd::uint8_t const*>(c)), length(rstd::strlen(c)) {}

    static constexpr auto from_raw_parts(rstd::uint8_t const* p [[clang::lifetimebound]],
                                         usize                len) noexcept -> Self {
        return { p, len };
    }
    static auto from_raw_parts(u8 const* p [[clang::lifetimebound]], usize len) noexcept -> Self {
        return { p, len };
    }

    /// Returns the underlying `OsStr`.
    constexpr auto as_os_str() const noexcept -> ref<OsStr> {
        return ref<OsStr>::from_raw_parts(p, length);
    }

    /// Attempts to yield a `ref<str>` if the path is valid UTF-8.
    constexpr auto to_str() const noexcept -> Option<ref<str>> { return as_os_str().to_str(); }

    /// Converts to a `String`, replacing invalid UTF-8 with U+FFFD.
    auto to_string_lossy() const -> String { return as_os_str().to_string_lossy(); }

    /// Returns `true` if the path starts with a root separator.
    constexpr auto is_absolute() const noexcept -> bool {
        if (length == usize {}) return false;
#if defined(RSTD_OS_WINDOWS)
        auto const len = length.to_primitive();
        // UNC or drive-letter absolute: \\... or C:\...
        if (len >= 2 && path_detail::is_sep(p[0]) && path_detail::is_sep(p[1])) return true;
        if (len >= 3 && p[1] == ':' && path_detail::is_sep(p[2])) return true;
        return false;
#else
        return p[0] == '/';
#endif
    }

    /// Returns `true` if the path is not absolute.
    constexpr auto is_relative() const noexcept -> bool { return ! is_absolute(); }

    /// Returns `true` if the path has a root component.
    constexpr auto has_root() const noexcept -> bool {
        return path_detail::root_len(p, length.to_primitive()) != 0;
    }

    /// Produces an iterator over the path components.
    constexpr auto components() const noexcept -> path::Components {
        return path::Components(p, length);
    }

    /// Returns `true` when `base` is a component-wise prefix of this path.
    constexpr auto starts_with(ref<path::Path> base) const noexcept -> bool {
        auto iter   = components();
        auto prefix = base.components();
        while (true) {
            auto b = prefix.next();
            if (b.is_none()) return true;
            auto s = iter.next();
            if (s.is_none()) return false;
            if (! (*s == *b)) return false;
        }
    }

    /// Strips `base` as a component-wise prefix and returns the remaining path.
    constexpr auto strip_prefix(ref<path::Path> base) const noexcept -> Option<ref<path::Path>> {
        auto iter   = components();
        auto prefix = base.components();
        while (true) {
            auto b = prefix.next();
            if (b.is_none()) return Some(iter.as_path());
            auto s = iter.next();
            if (s.is_none()) return None();
            if (! (*s == *b)) return None();
        }
    }

    /// Returns the parent path (everything before the last component).
    ///
    /// Returns `None` for root or empty paths.
    constexpr auto parent() const noexcept -> Option<ref<path::Path>> {
        if (length == usize {}) return None();
        auto const path_len = length.to_primitive();

        // Find where the file name starts
        auto i = path_detail::file_name_start(p, path_len);

        if (i == path_len) {
            // No file name (all separators, e.g. "/") → no parent
            return None();
        }
        if (i == 0) {
            // No separator found → single component, no parent
            return None();
        }

        // Strip trailing separators from parent, but keep at least one char
        // so "/" stays as "/" rather than becoming ""
        rstd::size_t pi = i;
        while (pi > 1 && path_detail::is_sep(p[pi - 1])) --pi;
        ref<path::Path> r(p, usize(pi));
        return Some(rstd::move(r));
    }

    /// Returns the final component of the path (file or directory name).
    constexpr auto file_name() const noexcept -> Option<ref<OsStr>> {
        if (length == usize {}) return None();
        auto const   path_len = length.to_primitive();
        rstd::size_t start    = path_detail::file_name_start(p, path_len);
        rstd::size_t end      = path_len;
        while (end > start && path_detail::is_sep(p[end - 1])) --end;
        if (end <= start) return None();
        ref<OsStr> r = ref<OsStr>::from_raw_parts(p + start, usize(end - start));
        return Some(rstd::move(r));
    }

    /// Returns the extension of the file name (after the last `.`).
    constexpr auto extension() const noexcept -> Option<ref<OsStr>> {
        auto fn = file_name();
        if (fn.is_none()) return None();
        auto name = *fn;
        // Find last '.' that is not the first character
        auto const   name_len = name.len().to_primitive();
        rstd::size_t dot      = name_len;
        for (rstd::size_t i = name_len; i > 1; --i) {
            if (name.data()[i - 1] == '.') {
                dot = i - 1;
                break;
            }
        }
        if (dot == name_len || dot == 0) return None();
        ref<OsStr> r = ref<OsStr>::from_raw_parts(name.data() + dot + 1, usize(name_len - dot - 1));
        return Some(rstd::move(r));
    }

    constexpr auto len() const noexcept -> usize { return length; }
    constexpr auto is_empty() const noexcept -> bool { return length == usize {}; }
    constexpr auto data() const noexcept -> rstd::uint8_t const* { return p; }

    /// Copies the path bytes into a NUL-terminated `CString` suitable for libc calls.
    /// Returns `Err(NulError)` if the path contains an interior NUL byte.
    auto to_cstring() const -> Result<CString, NulError> {
        auto bytes = slice<byte>::from_raw_parts(p, length);
        for (rstd::size_t i = 0; i < length.to_primitive(); ++i) {
            if (p[i] == 0) {
                auto v = Vec<u8>::copy_from_bytes(bytes);
                return Err(NulError { length, rstd::move(v) });
            }
        }
        auto v = Vec<u8>::copy_from_bytes(bytes);
        return Ok(CString::from_vec_unchecked(rstd::move(v)));
    }

    constexpr operator bool() const { return length != usize {} && p != nullptr; }

    constexpr auto deref() const noexcept -> ref<Target> { return *this; }
};

} // namespace rstd

export namespace rstd::path
{

constexpr auto Components::as_path() const noexcept -> ref<Path> {
    auto start = remaining_start();
    return ref<Path>::from_raw_parts(m_path + start, usize(m_len - start));
}

/// An owned, mutable filesystem path, analogous to Rust's `PathBuf`.
class PathBuf {
    OsString inner;

    explicit PathBuf(OsString&& s): inner(rstd::move(s)) {}

public:
    PathBuf()                              = default;
    PathBuf(PathBuf&&) noexcept            = default;
    PathBuf& operator=(PathBuf&&) noexcept = default;

    /// Creates an empty `PathBuf`.
    static auto make() -> PathBuf { return {}; }

    /// Creates a `PathBuf` from a `String`.
    static auto from(String&& s) -> PathBuf { return PathBuf { OsString::from(rstd::move(s)) }; }

    /// Creates a `PathBuf` from a `ref<str>`.
    static auto from(ref<str> s) -> PathBuf { return PathBuf { OsString::from(s) }; }

    /// Creates a `PathBuf` from a borrowed path.
    static auto from(ref<Path> s) -> PathBuf { return PathBuf { OsString::from(s.as_os_str()) }; }

    /// Creates a `PathBuf` from an `OsString`.
    static auto from(OsString&& s) -> PathBuf { return PathBuf { rstd::move(s) }; }

    /// Creates a `PathBuf` from a C string.
    static auto from(const char* s) -> PathBuf { return from(ref<str>(s)); }

    /// Returns a borrowed `ref<Path>`.
    auto as_path() const noexcept [[clang::lifetimebound]] -> ref<Path> {
        return ref<Path>(inner.as_os_str());
    }

    auto clone() const -> PathBuf { return PathBuf::from(as_path()); }

    void clone_from(const PathBuf& source) { *this = source.clone(); }

    /// Consumes the `PathBuf` and returns the inner `OsString`.
    auto into_os_string() -> OsString { return rstd::move(inner); }

    /// Extends the path with `component`.
    ///
    /// If `component` is absolute, it replaces the current path.
    /// Otherwise, a separator is inserted if needed and `component` is appended.
    void push(ref<Path> component) {
        auto      comp = component.as_os_str();
        ref<Path> comp_path(comp);
        if (comp_path.is_absolute()) {
            inner = OsString::from(comp);
            return;
        }
        // Add separator if current path is non-empty and doesn't end with one
        if (inner.len() != usize {}) {
            auto os_str = inner.as_os_str();
            auto bytes  = os_str.as_encoded_bytes();
            if (! path_detail::is_sep(bytes[bytes.len() - usize(1)])) {
                rstd::uint8_t sep = PATH_SEP;
                inner.push(ref<OsStr>::from_raw_parts(&sep, usize(1)));
            }
        }
        inner.push(comp);
    }

    /// Removes the last component. Returns `false` if already at root/empty.
    auto pop() -> bool {
        auto p = as_path().parent();
        if (p.is_none()) return false;
        auto parent = *p;
        inner       = OsString::from(parent.as_os_str());
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
    operator ref<Path>() const noexcept [[clang::lifetimebound]] { return as_path(); }
};

} // namespace rstd::path

// ── Display for ref<Path> ────────────────────────────────────────────────
namespace rstd
{

template<>
struct Impl<fmt::Display, ref<path::Path>> : ImplBase<ref<path::Path>> {
    auto fmt(fmt::Formatter& f) const -> bool {
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
struct Impl<fmt::Debug, ref<path::Path>> : ImplBase<ref<path::Path>> {
    auto fmt(fmt::Formatter& f) const -> bool {
        f.write_raw("\"", 1);
        as<fmt::Display>(this->self()).fmt(f);
        return f.write_raw("\"", 1);
    }
};

} // namespace rstd

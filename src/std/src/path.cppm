module;
#include <rstd/macro.hpp>
export module rstd:path;
export import rstd.ffi;
export import rstd.alloc;

using ::alloc::ffi::CString;
using ::alloc::ffi::NulError;
using ::alloc::string::String;
using ::alloc::vec::Vec;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace rstd::path
{

export constexpr auto is_separator(char32_t value) noexcept -> bool {
#if RSTD_OS_WINDOWS
    return value == U'/' || value == U'\\';
#else
    return value == U'/';
#endif
}

#if RSTD_OS_WINDOWS
export inline constexpr char32_t MAIN_SEPARATOR     = U'\\';
export inline constexpr ref<str> MAIN_SEPARATOR_STR = "\\"_str;
#else
export inline constexpr char32_t MAIN_SEPARATOR     = U'/';
export inline constexpr ref<str> MAIN_SEPARATOR_STR = "/"_str;
#endif

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
        case ComponentKind::RootDir: return ref<OsStr>("/"_str);
        case ComponentKind::CurDir: return ref<OsStr>("."_str);
        case ComponentKind::ParentDir: return ref<OsStr>(".."_str);
        case ComponentKind::Normal: return m_os;
        }
        return {};
    }

    constexpr auto operator==(const Component& other) const noexcept -> bool {
        if (m_kind != other.m_kind) return false;
        if (m_kind != ComponentKind::Normal) return true;
        if (m_os.len() != other.m_os.len()) return false;
        return m_os.as_encoded_bytes() == other.m_os.as_encoded_bytes();
    }

private:
    constexpr Component(ComponentKind kind, ref<OsStr> os) noexcept: m_kind(kind), m_os(os) {}

    ComponentKind m_kind { ComponentKind::Normal };
    ref<OsStr>    m_os {};
};

export class Components : public DefaultInClass<Components, iter::Iterator> {
public:
    using Item = Component;

    constexpr Components() noexcept = default;
    /// The encoded path bytes must stay readable for the lifetime of the iterator.
    [[unsafe]]
    constexpr Components(byte const* p [[clang::lifetimebound]], usize len) noexcept;

    constexpr auto next() noexcept -> Option<Component>;
    constexpr auto as_path() const noexcept -> ref<Path>;

private:
    constexpr auto remaining_start() const noexcept -> rstd::size_t;

    slice<u8>    m_path;
    rstd::size_t m_pos {};
    bool         m_root_pending { false };
    bool         m_cur_pending { false };
};

export class PathBuf;
export struct NormalizeError {};

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
inline constexpr u8   PATH_SEP         = u8('\\');
inline constexpr bool HAS_DRIVE_PREFIX = true;
#else
inline constexpr u8   PATH_SEP         = u8('/');
inline constexpr bool HAS_DRIVE_PREFIX = false;
#endif

namespace path_detail
{

constexpr bool is_sep(u8 c) {
    return path::is_separator(static_cast<char32_t>(c.to_primitive()));
}

constexpr auto root_len(slice<u8> bytes) -> rstd::size_t {
    if (bytes.is_empty()) return 0;
#if defined(RSTD_OS_WINDOWS)
    if (bytes.len() >= usize(3) && bytes[usize(1)] == u8(':') && is_sep(bytes[usize(2)])) return 3;
    return is_sep(bytes[usize()]) ? 1 : 0;
#else
    return bytes[usize()] == u8('/') ? 1 : 0;
#endif
}

constexpr auto skip_seps(slice<u8> bytes, rstd::size_t pos) -> rstd::size_t {
    while (usize(pos) < bytes.len() && is_sep(bytes[usize(pos)])) ++pos;
    return pos;
}

constexpr auto include_cur_dir(slice<u8> bytes) -> bool {
    if (root_len(bytes) != 0 || bytes.is_empty() || bytes[usize()] != u8('.')) return false;
    return bytes.len() == usize(1) || is_sep(bytes[usize(1)]);
}

} // namespace path_detail

namespace path
{

constexpr Components::Components(byte const* p, usize len) noexcept {
    [[unsafe]] { m_path = slice<u8>::from_raw_parts(p, len); }
    auto root = path_detail::root_len(m_path);
    if (root != 0) {
        m_root_pending = true;
        m_pos          = root;
    } else if (path_detail::include_cur_dir(m_path)) {
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

    auto length = m_path.len().to_primitive();
    while (true) {
        m_pos = path_detail::skip_seps(m_path, m_pos);
        if (m_pos >= length) return None();

        auto start = m_pos;
        while (m_pos < length && ! path_detail::is_sep(m_path[usize(m_pos)])) ++m_pos;
        auto len = m_pos - start;
        if (m_path[usize(start)] == u8('.')) {
            if (len == 1) continue;
            if (len == 2 && m_path[usize(start + 1)] == u8('.'))
                return Some(Component::parent_dir());
        }

        [[unsafe]] {
            return Some(Component::normal(ref<OsStr>::from_encoded_bytes_unchecked(
                slice<u8>::from_raw_parts(m_path.as_raw_ptr() + start, usize(len)))));
        }
    }
}

constexpr auto Components::remaining_start() const noexcept -> rstd::size_t {
    if (m_root_pending) return 0;
    if (m_cur_pending) return 0;
    return path_detail::skip_seps(m_path, m_pos);
}

} // namespace path

/// A borrowed reference to a filesystem path.
template<>
struct ref<path::Path> : ref_base<ref<path::Path>, byte[], false> {
    USE_TRAIT(ref)

private:
    friend ref_base<ref<path::Path>, byte[], false>;
    byte const* p { nullptr };
    usize       length {};

public:
    constexpr ref() noexcept = default;

    /// Construct from a `ref<OsStr>`.
    constexpr ref(ref<OsStr> s [[clang::lifetimebound]]) noexcept
        : p(s.as_encoded_bytes().as_raw_ptr()), length(s.len()) {}

    /// Construct from a `ref<str>`.
    constexpr ref(ref<str> s [[clang::lifetimebound]]) noexcept: ref(ref<OsStr>(s)) {}

    /// Returns the underlying `OsStr`.
    constexpr auto as_os_str() const noexcept -> ref<OsStr> {
        [[unsafe]] {
            return ref<OsStr>::from_encoded_bytes_unchecked(slice<u8>::from_raw_parts(p, length));
        }
    }

    /// Attempts to yield a `ref<str>` if the path is valid UTF-8.
    constexpr auto to_str() const noexcept -> Option<ref<str>> { return as_os_str().to_str(); }

    /// Converts to a `String`, replacing invalid UTF-8 with U+FFFD.
    auto to_string_lossy() const -> String { return as_os_str().to_string_lossy(); }

    /// Returns `true` if the path starts with a root separator.
    constexpr auto is_absolute() const noexcept -> bool {
        auto os_str = as_os_str();
        auto bytes  = os_str.as_encoded_bytes();
        if (bytes.is_empty()) return false;
#if defined(RSTD_OS_WINDOWS)
        if (bytes.len() >= usize(2) && path_detail::is_sep(bytes[usize()]) &&
            path_detail::is_sep(bytes[usize(1)]))
            return true;
        return bytes.len() >= usize(3) && bytes[usize(1)] == u8(':') &&
               path_detail::is_sep(bytes[usize(2)]);
#else
        return bytes[usize()] == u8('/');
#endif
    }

    /// Returns `true` if the path is not absolute.
    constexpr auto is_relative() const noexcept -> bool { return ! is_absolute(); }

    /// Returns `true` if the path has a root component.
    constexpr auto has_root() const noexcept -> bool {
        return path_detail::root_len(as_os_str().as_encoded_bytes()) != 0;
    }

    /// Returns `true` if the path is relative and cannot escape its joining directory.
    constexpr auto is_safe_relative() const noexcept -> bool {
        if (is_absolute() || has_root()) return false;
        auto values = components();
        return as<iter::Iterator>(values).all([](path::Component value) {
            return ! value.is_root_dir() && ! value.is_parent_dir();
        });
    }

    /// Produces an iterator over the path components.
    constexpr auto components() const noexcept -> path::Components {
        [[unsafe]] { return path::Components(p, length); }
    }

    /// Normalizes components without filesystem access; errors on escaping the lexical root.
    auto normalize_lexically() const -> Result<path::PathBuf, path::NormalizeError>;

    constexpr auto operator==(ref<path::Path> other) const noexcept -> bool {
        auto left  = components();
        auto right = other.components();
        return as<iter::Iterator>(left).eq(rstd::move(right));
    }

    /// Returns `true` when `base` is a component-wise prefix of this path.
    constexpr auto starts_with(ref<path::Path> base) const noexcept -> bool {
        auto iter   = components();
        auto prefix = base.components();
        return as<iter::Iterator>(prefix).all([&iter](path::Component expected) {
            auto value = iter.next();
            return value.is_some() && *value == expected;
        });
    }

    /// Strips `base` as a component-wise prefix and returns the remaining path.
    constexpr auto strip_prefix(ref<path::Path> base) const noexcept -> Option<ref<path::Path>> {
        auto iter    = components();
        auto prefix  = base.components();
        auto matches = as<iter::Iterator>(prefix).all([&iter](path::Component expected) {
            auto value = iter.next();
            return value.is_some() && *value == expected;
        });
        return matches ? Some(iter.as_path()) : None();
    }

    /// Returns the parent path (everything before the last component).
    ///
    /// Returns `None` for root or empty paths.
    constexpr auto parent() const noexcept -> Option<ref<path::Path>> {
        auto                    values = components();
        auto                    end    = usize {};
        auto                    os_str = as_os_str();
        auto                    bytes  = os_str.as_encoded_bytes();
        Option<ref<path::Path>> parent;
        while (auto component = values.next()) {
            if (component->is_root_dir()) {
                end = usize(path_detail::root_len(bytes));
                continue;
            }
            [[unsafe]] {
                parent = Some(ref<path::Path>(
                    ref<OsStr>::from_encoded_bytes_unchecked(slice<u8>::from_raw_parts(p, end))));
            }
            end = length - values.as_path().len();
            while (end > usize {} && path_detail::is_sep(bytes[end - usize(1)])) --end;
        }
        return parent;
    }

    /// Returns the final component of the path (file or directory name).
    constexpr auto file_name() const noexcept -> Option<ref<OsStr>> {
        Option<ref<OsStr>> name;
        auto               values = components();
        while (auto component = values.next())
            name = component->is_normal() ? Some(component->as_os_str()) : None();
        return name;
    }

    /// Returns the file name without its final extension.
    constexpr auto file_stem() const noexcept -> Option<ref<OsStr>> {
        auto name = file_name();
        if (name.is_none()) return None();
        auto bytes = name->as_encoded_bytes();
        auto end   = bytes.len();
        for (auto i = end; i > usize(1); --i) {
            if (bytes[i - usize(1)] == u8('.')) {
                end = i - usize(1);
                break;
            }
        }
        [[unsafe]] {
            return Some(ref<OsStr>::from_encoded_bytes_unchecked(
                slice<u8>::from_raw_parts(bytes.as_raw_ptr(), end)));
        }
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
            if (name.as_encoded_bytes()[usize(i - 1)] == u8('.')) {
                dot = i - 1;
                break;
            }
        }
        if (dot == name_len || dot == 0) return None();
        auto bytes = name.as_encoded_bytes();
        [[unsafe]] {
            return Some(ref<OsStr>::from_encoded_bytes_unchecked(slice<u8>::from_raw_parts(
                bytes.as_raw_ptr() + dot + 1, usize(name_len - dot - 1))));
        }
    }

    constexpr auto len() const noexcept -> usize { return length; }
    constexpr auto is_empty() const noexcept -> bool { return length == usize {}; }
    /// Copies the path bytes into a NUL-terminated `CString` suitable for libc calls.
    /// Returns `Err(NulError)` if the path contains an interior NUL byte.
    auto to_cstring() const -> Result<CString, NulError> {
        return CString::make(Vec<u8>::from(as_os_str().as_encoded_bytes()));
    }

    constexpr operator bool() const { return length != usize {} && p != nullptr; }

    constexpr auto deref() const noexcept -> ref<path::Path> { return *this; }
};

} // namespace rstd

export namespace rstd::path
{

constexpr auto Components::as_path() const noexcept -> ref<Path> {
    auto start = remaining_start();
    [[unsafe]] {
        const auto* data = m_path.as_raw_ptr();
        if (start != 0) data += start;
        auto bytes = slice<u8>::from_raw_parts(data, m_path.len() - usize(start));
        return ref<Path>(ref<OsStr>::from_encoded_bytes_unchecked(bytes));
    }
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

    /// Returns a borrowed `ref<Path>`.
    auto as_path() const noexcept [[clang::lifetimebound]] -> ref<Path> {
        return ref<Path>(inner.as_os_str());
    }

    auto capacity() const noexcept -> usize { return inner.capacity(); }

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
            if (! path_detail::is_sep(bytes.last().unwrap().get())) {
                array<u8, 1> separator(PATH_SEP);
                [[unsafe]] {
                    inner.push(ref<OsStr>::from_encoded_bytes_unchecked(separator.as_slice()));
                }
            }
        }
        inner.push(comp);
    }

    /// Removes the last component. Returns `false` if already at root/empty.
    auto pop() -> bool {
        auto p = as_path().parent();
        if (p.is_none()) return false;
        inner.truncate(p->len());
        return true;
    }

    /// Creates a new `PathBuf` by joining this path with a component.
    auto join(ref<Path> component) const -> PathBuf {
        auto result = PathBuf::from(OsString::from(inner.as_os_str()));
        result.push(component);
        return result;
    }

    auto len() const noexcept -> usize { return inner.len(); }
    /// Replaces the final component's extension, without a leading dot.
    /// Returns false without a file stem; panics if the extension contains a separator.
    auto set_extension(ref<OsStr> extension) -> bool {
        auto extension_bytes = extension.as_encoded_bytes();
        for (auto byte : extension_bytes)
            if (path_detail::is_sep(byte))
                rstd::panic { "extension cannot contain path separators" };
        auto stem = as_path().file_stem();
        if (stem.is_none()) return false;
        auto            stem_bytes = stem->as_encoded_bytes();
        auto            path_os    = inner.as_os_str();
        auto            bytes      = path_os.as_encoded_bytes();
        usize           end;
        rstd::uintptr_t start, source;
        [[unsafe]] {
            end    = usize(stem_bytes.as_raw_ptr() - bytes.as_raw_ptr()) + stem_bytes.len();
            start  = reinterpret_cast<rstd::uintptr_t>(bytes.as_raw_ptr());
            source = reinterpret_cast<rstd::uintptr_t>(extension_bytes.as_raw_ptr());
        }
        // C++ permits an extension borrowed from this path; truncation would invalidate it.
        if (! extension.is_empty() && source >= start &&
            source - start < bytes.len().to_primitive()) {
            auto owned = extension.to_os_string();
            return set_extension(owned.as_os_str());
        }
        inner.truncate(end);
        if (! extension.is_empty()) {
            inner.reserve_exact(extension.len() + usize(1));
            inner.push(ref<OsStr>("."_str));
            inner.push(extension);
        }
        return true;
    }

    auto is_empty() const noexcept -> bool { return inner.is_empty(); }

    /// Implicit conversion to `ref<Path>`.
    operator ref<Path>() const noexcept [[clang::lifetimebound]] { return as_path(); }
};

/// Computes a lexical path from `base` to `target` without accessing the filesystem.
auto lexically_relative(ref<Path> base, ref<Path> target) -> Option<PathBuf> {
    if (base.is_absolute() != target.is_absolute() || base.has_root() != target.has_root()) {
        return None();
    }
    auto base_parts   = Vec<OsString>::make();
    auto target_parts = Vec<OsString>::make();
    auto append_parts = [](ref<Path> value, Vec<OsString>& output) -> bool {
        auto components = value.components();
        for (auto component : iter::for_range(components)) {
            if (component.is_root_dir() || component.is_cur_dir()) continue;
            if (component.is_parent_dir()) return false;
            output.push(OsString::from(component.as_os_str()));
        }
        return true;
    };
    if (! append_parts(base, base_parts) || ! append_parts(target, target_parts)) return None();

    auto common = usize {};
    while (common < base_parts.len() && common < target_parts.len() &&
           base_parts[common].as_os_str().as_encoded_bytes() ==
               target_parts[common].as_os_str().as_encoded_bytes()) {
        ++common;
    }
    auto result = PathBuf::make();
    for (auto index = common; index < base_parts.len(); ++index) {
        result.push(PathBuf::from(".."_str).as_path());
    }
    for (auto index = common; index < target_parts.len(); ++index) {
        result.push(PathBuf::from(OsString::from(target_parts[index].as_os_str())).as_path());
    }
    return Some(result.is_empty() ? PathBuf::from("."_str) : rstd::move(result));
}

} // namespace rstd::path

// ── Display for ref<Path> ────────────────────────────────────────────────
namespace rstd
{

auto ref<path::Path>::normalize_lexically() const -> Result<path::PathBuf, path::NormalizeError> {
    auto lexical = path::PathBuf::make();
    auto values  = components();
    auto root    = usize {};
    bool first   = true;
    while (auto component = values.next()) {
        if (first && component->is_root_dir()) {
            auto length = path_detail::root_len(as_os_str().as_encoded_bytes());
            [[unsafe]] {
                lexical =
                    path::PathBuf::from(ref<path::Path>(ref<OsStr>::from_encoded_bytes_unchecked(
                        slice<u8>::from_raw_parts(p, usize(length)))));
            }
            root = lexical.len();
        } else if (first && component->is_cur_dir()) {
            lexical.push(ref<path::Path>(component->as_os_str()));
            root = lexical.len();
        } else if (component->is_parent_dir()) {
            if (lexical.len() == root) return Err(path::NormalizeError {});
            (void)lexical.pop();
        } else if (component->is_normal()) {
            lexical.push(ref<path::Path>(component->as_os_str()));
        }
        first = false;
    }
    return Ok(rstd::move(lexical));
}

template<>
struct Impl<fmt::Display, path::NormalizeError> : ImplBase<path::NormalizeError> {
    auto fmt(fmt::Formatter& f) const -> bool {
        return f.write_str("path contains a parent directory that escapes the lexical root"_str);
    }
};

template<>
struct Impl<fmt::Debug, path::NormalizeError> : ImplBase<path::NormalizeError> {
    auto fmt(fmt::Formatter& f) const -> bool { return f.write_str("NormalizeError"_str); }
};

template<>
struct Impl<error::Error, path::NormalizeError>
    : DefaultInImpl<error::Error, path::NormalizeError> {};

template<>
struct Impl<fmt::Display, ref<path::Path>> : ImplBase<ref<path::Path>> {
    auto fmt(fmt::Formatter& f) const -> bool {
        auto os_str  = this->self().as_os_str();
        auto display = os_str.display();
        return as<fmt::Display>(display).fmt(f);
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

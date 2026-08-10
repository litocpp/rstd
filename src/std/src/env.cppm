module;
#include <rstd/macro.hpp>
export module rstd:env;
export import :path;
export import :ffi;
export import rstd.alloc;

using ::alloc::string::String;
using ::alloc::vec::Vec;
using rstd::path::PathBuf;
using ::alloc::ffi::CString;
using rstd::ffi::CStr;
using rstd::ffi::OsStr;
using rstd::ffi::OsString;

using namespace rstd::prelude;
using namespace rstd::literals;

auto os_string_from_cstr(const char* s) -> OsString {
    auto vec = Vec<u8>::from(CStr::from_ptr(s).to_bytes());
    return OsString::from_encoded_bytes_unchecked(rstd::move(vec));
}

auto string_from_os_string(OsString value) -> String {
    auto result = value.into_string();
    if (result.is_err()) rstd::panic("command-line argument is not valid Unicode");
    return rstd::move(result).unwrap();
}

export namespace rstd::env
{

/// The error returned when a path list contains a platform separator that
/// cannot be represented by `join_paths()`.
struct JoinPathsError {};

/// An owning iterator over paths parsed from a platform-native path list.
using SplitPaths = ::alloc::vec::VecIntoIter<PathBuf>;

/// Parses a platform-native path list using the host `PATH` conventions.
auto split_paths(ref<OsStr> unparsed) -> SplitPaths {
    auto result = Vec<PathBuf>::make();
    auto bytes  = unparsed.as_encoded_bytes();
    auto part   = Vec<u8>::make();
#if RSTD_OS_WINDOWS
    auto quoted = false;
    for (auto value : bytes) {
        if (value == u8('"')) {
            quoted = ! quoted;
        } else if (value == u8(';') && ! quoted) {
            result.push(PathBuf::from(OsString::from_encoded_bytes_unchecked(rstd::move(part))));
            part = Vec<u8>::make();
        } else {
            part.emplace_back(value);
        }
    }
#else
    for (auto value : bytes) {
        if (value == u8(':')) {
            result.push(PathBuf::from(OsString::from_encoded_bytes_unchecked(rstd::move(part))));
            part = Vec<u8>::make();
        } else {
            part.emplace_back(value);
        }
    }
#endif
    result.push(PathBuf::from(OsString::from_encoded_bytes_unchecked(rstd::move(part))));
    return result.into_iter();
}

/// Joins paths using the host `PATH` conventions.
auto join_paths(slice<PathBuf> paths) -> Result<OsString, JoinPathsError> {
    auto result = OsString::make();
    for (rstd::size_t index = 0; index < paths.len().to_primitive(); ++index) {
        auto path  = paths[usize(index)].as_path();
        auto os    = path.as_os_str();
        auto bytes = os.as_encoded_bytes();
        if (index != 0) {
#if RSTD_OS_WINDOWS
            result.push(";"_str);
#else
            result.push(":"_str);
#endif
        }
#if RSTD_OS_WINDOWS
        auto quote = false;
        for (auto value : bytes) {
            if (value == u8('"')) return Err(JoinPathsError {});
            if (value == u8(';')) quote = true;
        }
        if (quote) result.push("\""_str);
        result.push(ref<OsStr>::from_encoded_bytes_unchecked(bytes));
        if (quote) result.push("\""_str);
#else
        for (auto value : bytes) {
            if (value == u8(':')) return Err(JoinPathsError {});
        }
        result.push(ref<OsStr>::from_encoded_bytes_unchecked(bytes));
#endif
    }
    return Ok(rstd::move(result));
}

/// Fetches the environment variable `key` without requiring Unicode.
///
/// Returns `None` if the variable is not set.
auto var_os(ref<OsStr> key) -> Option<OsString>;

/// Fetches the environment variable `key` from the current process.
///
/// Returns `None` if the variable is not set.
/// Panics if the value is not valid Unicode; use `var_os()` to preserve it.
///
/// \param key  Null-terminated name of the environment variable.
/// \return The value as a `String`, or `None`.
auto var(ref<OsStr> key) -> Option<String> {
    auto value = var_os(key);
    if (value.is_none()) return None();
    auto converted = rstd::move(value).unwrap().into_string();
    if (converted.is_err()) rstd::panic("environment value is not valid Unicode");
    return Some(rstd::move(converted).unwrap());
}

/// Returns the directory used for temporary files.
auto temp_dir() -> PathBuf {
#if RSTD_OS_UNIX
    auto configured = var("TMPDIR"_str);
    if (configured.is_some() && ! configured->is_empty()) {
        return PathBuf::from(rstd::move(configured).unwrap());
    }
    return PathBuf::from("/tmp"_str);
#else
    auto configured = var("TEMP"_str);
    if (configured.is_some() && ! configured->is_empty()) {
        return PathBuf::from(rstd::move(configured).unwrap());
    }
    configured = var("TMP"_str);
    if (configured.is_some() && ! configured->is_empty()) {
        return PathBuf::from(rstd::move(configured).unwrap());
    }
    return PathBuf::from("."_str);
#endif
}

/// Sets the environment variable `key` to `value` in the current process.
///
/// Not thread-safe on Unix platforms.
///
/// \param key    Null-terminated name of the environment variable.
/// \param value  Null-terminated value to set.
void set_var(ref<OsStr> key, ref<OsStr> value);

/// Removes the environment variable `key` from the current process.
///
/// Not thread-safe on Unix platforms.
///
/// \param key  Null-terminated name of the environment variable to remove.
void remove_var(ref<OsStr> key);

/// An owning iterator over command-line arguments as platform-native strings.
using ArgsOs = ::alloc::vec::VecIntoIter<OsString>;

class Args;
auto args() -> Args;

/// An owning iterator over command-line arguments, yielding each as a `String`.
class Args : public DefaultInClass<Args, iter::Iterator> {
    ArgsOs inner_;

    explicit Args(ArgsOs inner): inner_(rstd::move(inner)) {}

    friend auto args() -> Args;

public:
    using Item                                = String;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;

    auto next() -> Option<String> {
        auto value = inner_.next();
        if (value.is_none()) return None();
        return Some(string_from_os_string(rstd::move(value).unwrap()));
    }

    auto next_back() -> Option<String> {
        auto value = inner_.next_back();
        if (value.is_none()) return None();
        return Some(string_from_os_string(rstd::move(value).unwrap()));
    }

    auto size_hint() const -> iter::SizeHint { return inner_.size_hint(); }

    auto len() const -> usize { return inner_.len(); }
};

/// Returns the command-line arguments without requiring UTF-8.
auto args_os() -> ArgsOs;

/// Returns the command-line arguments of the current process.
///
/// Panics during iteration if an argument is not valid Unicode. Use `args_os()`
/// to preserve platform-native strings without validation.
///
/// On Linux/glibc the arguments are captured automatically at startup. On other
/// platforms (or when capture is unavailable) call `args_init` from `main` first.
auto args() -> Args;

/// Manually provides `argc`/`argv` (e.g. from `main`) for platforms where automatic
/// startup capture is unavailable. Safe to call before `args()`.
void args_init(int argc, char const* const* argv);

} // namespace rstd::env

namespace rstd
{

template<>
struct Impl<fmt::Display, env::JoinPathsError> : ImplBase<env::JoinPathsError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
#if RSTD_OS_WINDOWS
        return formatter.write_str("path segment contains double quote"_str);
#else
        return formatter.write_str("path segment contains separator ':'"_str);
#endif
    }
};

template<>
struct Impl<fmt::Debug, env::JoinPathsError> : ImplBase<env::JoinPathsError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_str("JoinPathsError"_str);
    }
};

template<>
struct Impl<error::Error, env::JoinPathsError> : DefaultInImpl<error::Error, env::JoinPathsError> {
};

} // namespace rstd

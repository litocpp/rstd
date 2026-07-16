export module rstd:env;
export import :path;
export import :sys;
export import :ffi;
export import rstd.alloc;

using ::alloc::string::String;
using ::alloc::vec::Vec;
using rstd::path::PathBuf;
using rstd::ffi::OsString;

using namespace rstd::prelude;

auto os_string_from_cstr(const char* s) -> OsString {
    auto len = rstd::strlen(s);
    auto vec = Vec<u8>::with_capacity(len);
    for (usize i = 0; i < len; i++) vec.push(static_cast<u8>(s[i]));
    return OsString::from_encoded_bytes_unchecked(rstd::move(vec));
}

auto string_from_os_string(OsString value) -> String {
    auto result = value.into_string();
    if (result.is_err()) rstd::panic("command-line argument is not valid Unicode");
    return rstd::move(result).unwrap();
}

export namespace rstd::env
{

/// Fetches the environment variable `key` without requiring Unicode.
///
/// Returns `None` if the variable is not set.
auto var_os(const char* key) -> Option<OsString> {
    auto* value = sys::getenv_internal(key);
    if (value == nullptr) return None();
    return Some(os_string_from_cstr(value));
}

/// Fetches the environment variable `key` from the current process.
///
/// Returns `None` if the variable is not set.
/// Panics if the value is not valid Unicode; use `var_os()` to preserve it.
///
/// \param key  Null-terminated name of the environment variable.
/// \return The value as a `String`, or `None`.
auto var(const char* key) -> Option<String> {
    auto value = var_os(key);
    if (value.is_none()) return None();
    auto converted = rstd::move(value).unwrap().into_string();
    if (converted.is_err()) rstd::panic("environment value is not valid Unicode");
    return Some(rstd::move(converted).unwrap());
}

/// Returns the directory used for temporary files.
auto temp_dir() -> PathBuf {
#if RSTD_OS_UNIX
    auto configured = var("TMPDIR");
    if (configured.is_some() && ! configured->is_empty()) {
        return PathBuf::from(rstd::move(configured).unwrap());
    }
    return PathBuf::from("/tmp");
#else
    auto configured = var("TEMP");
    if (configured.is_some() && ! configured->is_empty()) {
        return PathBuf::from(rstd::move(configured).unwrap());
    }
    configured = var("TMP");
    if (configured.is_some() && ! configured->is_empty()) {
        return PathBuf::from(rstd::move(configured).unwrap());
    }
    return PathBuf::from(".");
#endif
}

/// Sets the environment variable `key` to `value` in the current process.
///
/// Not thread-safe on Unix platforms.
///
/// \param key    Null-terminated name of the environment variable.
/// \param value  Null-terminated value to set.
void set_var(const char* key, const char* value) {
    sys::setenv_internal(key, value);
}

/// Removes the environment variable `key` from the current process.
///
/// Not thread-safe on Unix platforms.
///
/// \param key  Null-terminated name of the environment variable to remove.
void remove_var(const char* key) {
    sys::unsetenv_internal(key);
}

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
    using Item = String;

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
auto args_os() -> ArgsOs {
    auto raw = sys::args_argc_argv();
    auto n   = raw.argc < 0 ? usize(0) : static_cast<usize>(raw.argc);
    auto vec = Vec<OsString>::with_capacity(n);
    for (isize i = 0; i < raw.argc; ++i) {
        const char* p = raw.argv[i];
        if (p == nullptr) break;
        vec.push(os_string_from_cstr(p));
    }
    return vec.into_iter();
}

/// Returns the command-line arguments of the current process.
///
/// Panics during iteration if an argument is not valid Unicode. Use `args_os()`
/// to preserve platform-native strings without validation.
///
/// On Linux/glibc the arguments are captured automatically at startup. On other
/// platforms (or when capture is unavailable) call `args_init` from `main` first.
auto args() -> Args {
    return Args(args_os());
}

/// Manually provides `argc`/`argv` (e.g. from `main`) for platforms where automatic
/// startup capture is unavailable. Safe to call before `args()`.
void args_init(int argc, char const* const* argv) {
    sys::args_capture(static_cast<isize>(argc), argv);
}

} // namespace rstd::env

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

namespace env_detail
{
auto string_from_cstr(const char* s) -> String {
    auto len = rstd::strlen(s);
    auto vec = Vec<u8>::with_capacity(len);
    for (usize i = 0; i < len; i++) vec.push(static_cast<u8>(s[i]));
    return String::from_utf8_unchecked(rstd::move(vec));
}

auto os_string_from_cstr(const char* s) -> OsString {
    auto len = rstd::strlen(s);
    auto vec = Vec<u8>::with_capacity(len);
    for (usize i = 0; i < len; i++) vec.push(static_cast<u8>(s[i]));
    return OsString::from_encoded_bytes_unchecked(rstd::move(vec));
}
} // namespace env_detail

export namespace rstd::env
{

/// Fetches the environment variable `key` from the current process.
///
/// Returns `None` if the variable is not set.
///
/// \param key  Null-terminated name of the environment variable.
/// \return The value as a `String`, or `None`.
auto var(const char* key) -> Option<String> {
    auto* val = sys::getenv_internal(key);
    if (val == nullptr) {
        return None();
    }
    return Some(env_detail::string_from_cstr(val));
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

/// An owning iterator over the command-line arguments, yielding each as a `String`.
///
/// The first element is conventionally the program path. Implements
/// `Iterator`, `DoubleEndedIterator` and `ExactSizeIterator`.
using Args = ::alloc::vec::VecIntoIter<String>;

/// An owning iterator over command-line arguments as platform-native strings.
using ArgsOs = ::alloc::vec::VecIntoIter<OsString>;

/// Returns the command-line arguments without requiring UTF-8.
auto args_os() -> ArgsOs {
    auto raw = sys::args_argc_argv();
    auto n   = raw.argc < 0 ? usize(0) : static_cast<usize>(raw.argc);
    auto vec = Vec<OsString>::with_capacity(n);
    for (isize i = 0; i < raw.argc; ++i) {
        const char* p = raw.argv[i];
        if (p == nullptr) break;
        vec.push(env_detail::os_string_from_cstr(p));
    }
    return vec.into_iter();
}

/// Returns the command-line arguments of the current process.
///
/// On Linux/glibc the arguments are captured automatically at startup. On other
/// platforms (or when capture is unavailable) call `args_init` from `main` first.
auto args() -> Args {
    auto raw = sys::args_argc_argv();
    auto n   = raw.argc < 0 ? usize(0) : static_cast<usize>(raw.argc);
    auto vec = Vec<String>::with_capacity(n);
    for (isize i = 0; i < raw.argc; ++i) {
        const char* p = raw.argv[i];
        if (p == nullptr) break;
        vec.push(env_detail::string_from_cstr(p));
    }
    return vec.into_iter();
}

/// Manually provides `argc`/`argv` (e.g. from `main`) for platforms where automatic
/// startup capture is unavailable. Safe to call before `args()`.
void args_init(int argc, char const* const* argv) {
    sys::args_capture(static_cast<isize>(argc), argv);
}

} // namespace rstd::env

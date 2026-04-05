export module rstd:env;
export import :sys;
export import rstd.alloc;

using ::alloc::string::String;
using ::alloc::vec::Vec;

using namespace rstd::prelude;

namespace env_detail
{
auto string_from_cstr(const char* s) -> String {
    auto len = rstd::strlen(s);
    auto vec = Vec<u8>::with_capacity(len);
    for (usize i = 0; i < len; i++) vec.push(static_cast<u8>(s[i]));
    return String::from_utf8_unchecked(rstd::move(vec));
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

} // namespace rstd::env

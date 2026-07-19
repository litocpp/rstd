module;

module rstd;
import :env;
import :sys.pal;

namespace rstd::env
{

auto var_os(const char* key) -> Option<ffi::OsString> {
    auto* value = sys::pal::getenv_internal(key);
    if (value == nullptr) return None();
    return Some(os_string_from_cstr(value));
}

void set_var(const char* key, const char* value) {
    sys::pal::setenv_internal(key, value);
}

void remove_var(const char* key) {
    sys::pal::unsetenv_internal(key);
}

auto args_os() -> ArgsOs {
    auto raw    = sys::pal::args_argc_argv();
    auto len    = raw.argc < 0 ? rstd::size_t(0) : static_cast<rstd::size_t>(raw.argc);
    auto values = Vec<ffi::OsString>::with_capacity(usize(len));
    for (rstd::size_t i = 0; i < len; ++i) {
        const char* value = raw.argv[i];
        if (value == nullptr) break;
        values.push(os_string_from_cstr(value));
    }
    return values.into_iter();
}

auto args() -> Args {
    return Args(args_os());
}

void args_init(int argc, char const* const* argv) {
    sys::pal::args_capture(argc, argv);
}

} // namespace rstd::env

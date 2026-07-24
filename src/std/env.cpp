module;

module rstd;
import :env;
import :sys.pal;

namespace rstd::env
{

namespace
{

auto cstring(ref<ffi::OsStr> value) -> CString {
    return CString::make(Vec<u8>::from(value.as_encoded_bytes())).unwrap();
}

} // namespace

auto var_os(ref<ffi::OsStr> key) -> Option<ffi::OsString> {
    auto  key_value = cstring(key);
    auto* value     = sys::pal::getenv_internal(key_value.as_ptr());
    if (value == nullptr) return None();
    return Some(os_string_from_cstr(value));
}

void set_var(ref<ffi::OsStr> key, ref<ffi::OsStr> value) {
    auto key_value = cstring(key);
    auto env_value = cstring(value);
    sys::pal::setenv_internal(key_value.as_ptr(), env_value.as_ptr());
}

void remove_var(ref<ffi::OsStr> key) {
    auto key_value = cstring(key);
    sys::pal::unsetenv_internal(key_value.as_ptr());
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

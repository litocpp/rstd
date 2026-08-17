module rstd;
import :sys.pal.unix;
import :sys.libc;
import rstd.core;

namespace rstd::sys::pal::unix
{
[[noreturn]]
void abort_internal() {
    libc::abort();
}

[[noreturn]]
void exit_internal(int code) {
    libc::_exit(code);
}

auto getpid_internal() -> u32 {
    return u32(libc::getpid());
}

auto getenv_internal(const char* name) -> const char* {
    return libc::getenv(name);
}

auto setenv_internal(const char* name, const char* value) -> bool {
    return libc::setenv(name, value, 1) == 0;
}

auto unsetenv_internal(const char* name) -> bool {
    return libc::unsetenv(name) == 0;
}

// Command-line argument capture.
//
// glibc passes (argc, argv, envp) to functions in `.init_array` as a non-standard
// extension, so we register a capturing function there and stash the raw argc/argv.
// This makes `env::args()` work without a runtime `main` wrapper.
namespace args_detail
{
inline int                g_argc {};
inline char const* const* g_argv = nullptr;

extern "C" inline void rstd_capture_args(int argc, char** argv, char**) {
    g_argc = argc;
    g_argv = argv;
}

using init_fn_t = void (*)(int, char**, char**);

[[gnu::used, gnu::retain, gnu::section(".init_array.00099")]]
inline init_fn_t rstd_args_init_entry = &rstd_capture_args;
} // namespace args_detail

void args_capture(int argc, char const* const* argv) {
    args_detail::g_argc = argc;
    args_detail::g_argv = argv;
}

auto args_argc_argv() -> ArgcArgv {
    if (args_detail::g_argv == nullptr) return { 0, nullptr };
    return { args_detail::g_argc, args_detail::g_argv };
}

} // namespace rstd::sys::pal::unix

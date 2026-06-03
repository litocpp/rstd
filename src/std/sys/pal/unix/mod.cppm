module;
#include <rstd/macro.hpp>
export module rstd:sys.pal.unix;
export import :sys.pal.unix.futex;
export import :sys.pal.unix.sync;
export import :sys.pal.unix.time;

#if RSTD_OS_UNIX
namespace rstd::sys::pal::unix
{
export using pal::unix::sync::mutex::Mutex;
export using pal::unix::sync::condvar::Condvar;
export using pal::unix::time::Instant;
export using pal::unix::time::SystemTime;

export [[noreturn]]
void abort_internal() {
    libc::abort();
}

export [[noreturn]]
void exit_internal(int code) {
    libc::_exit(code);
}

export auto getpid_internal() -> u32 {
    return static_cast<u32>(libc::getpid());
}

export auto getenv_internal(const char* name) -> const char* {
    return libc::getenv(name);
}

export auto setenv_internal(const char* name, const char* value) -> bool {
    return libc::setenv(name, value, 1) == 0;
}

export auto unsetenv_internal(const char* name) -> bool {
    return libc::unsetenv(name) == 0;
}

// Command-line argument capture.
//
// glibc passes (argc, argv, envp) to functions in `.init_array` as a non-standard
// extension, so we register a capturing function there and stash the raw argc/argv.
// This makes `env::args()` work without a runtime `main` wrapper.
namespace args_detail
{
inline isize              g_argc = 0;
inline char const* const* g_argv = nullptr;

extern "C" inline void rstd_capture_args(int argc, char** argv, char**) {
    g_argc = static_cast<isize>(argc);
    g_argv = argv;
}

using init_fn_t = void (*)(int, char**, char**);

[[gnu::used, gnu::retain, gnu::section(".init_array.00099")]]
inline init_fn_t rstd_args_init_entry = &rstd_capture_args;
} // namespace args_detail

/// Raw, system-provided command-line argument vector.
export struct ArgcArgv {
    isize              argc;
    char const* const* argv;
};

/// Overrides the captured argc/argv (e.g. when `.init_array` capture is unavailable).
export void args_capture(isize argc, char const* const* argv) {
    args_detail::g_argc = argc;
    args_detail::g_argv = argv;
}

/// Returns the unmodified system-provided argc/argv, or `{0, nullptr}` if unset.
export auto args_argc_argv() -> ArgcArgv {
    if (args_detail::g_argv == nullptr) return { 0, nullptr };
    return { args_detail::g_argc, args_detail::g_argv };
}

} // namespace rstd::sys::pal::unix
#endif

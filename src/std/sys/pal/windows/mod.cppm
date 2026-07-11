module;
#include <rstd/macro.hpp>

#if RSTD_OS_WINDOWS
#include <process.h>
#include <stdlib.h>
#endif
export module rstd:sys.pal.windows;
export import :sys.pal.windows.futex;
export import :sys.pal.windows.sync;
export import :sys.pal.windows.time;

#if RSTD_OS_WINDOWS
import :sys.libc.windows;

namespace rstd::sys::pal::windows
{
export using pal::windows::sync::mutex::Mutex;
export using pal::windows::sync::condvar::Condvar;
export using pal::windows::time::Instant;
export using pal::windows::time::SystemTime;

export [[noreturn]]
void abort_internal() {
    libc::RaiseFailFastException(nullptr, nullptr, 0x2);
    __builtin_unreachable();
}

export [[noreturn]]
void exit_internal(int code) {
    libc::ExitProcess(static_cast<unsigned>(code));
    __builtin_unreachable();
}

export auto getpid_internal() -> u32 {
    return static_cast<u32>(libc::GetCurrentProcessId());
}

export auto getenv_internal(const char* name) -> const char* {
    return ::getenv(name);
}

export auto setenv_internal(const char* name, const char* value) -> bool {
    return libc::_putenv_s(name, value) == 0;
}

export auto unsetenv_internal(const char* name) -> bool {
    return libc::_putenv_s(name, "") == 0;
}

/// Raw, system-provided command-line argument vector.
export struct ArgcArgv {
    isize              argc;
    char const* const* argv;
};

namespace args_detail
{
inline isize              g_argc = 0;
inline char const* const* g_argv = nullptr;
} // namespace args_detail

/// Overrides the captured argc/argv (call from `main` on Windows).
export void args_capture(isize argc, char const* const* argv) {
    args_detail::g_argc = argc;
    args_detail::g_argv = argv;
}

/// Returns the captured argc/argv, or `{0, nullptr}` if `args_capture` was not called.
export auto args_argc_argv() -> ArgcArgv {
    if (args_detail::g_argv == nullptr)
        return { static_cast<isize>(__argc), reinterpret_cast<char const* const*>(__argv) };
    return { args_detail::g_argc, args_detail::g_argv };
}

} // namespace rstd::sys::pal::windows
#endif

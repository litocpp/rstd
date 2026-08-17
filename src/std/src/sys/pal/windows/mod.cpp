module;
#include <process.h>
#include <stdlib.h>
module rstd;
import :sys.pal.windows;

import :sys.libc.windows;
import rstd.core;

namespace rstd::sys::pal::windows
{
[[noreturn]]
void abort_internal() {
    libc::RaiseFailFastException(nullptr, nullptr, 0x2);
    __builtin_unreachable();
}

[[noreturn]]
void exit_internal(int code) {
    libc::ExitProcess(static_cast<unsigned>(code));
    __builtin_unreachable();
}

auto getpid_internal() -> u32 {
    return u32(libc::GetCurrentProcessId());
}

auto getenv_internal(const char* name) -> const char* {
    return ::getenv(name);
}

auto setenv_internal(const char* name, const char* value) -> bool {
    return libc::_putenv_s(name, value) == 0;
}

auto unsetenv_internal(const char* name) -> bool {
    return libc::_putenv_s(name, "") == 0;
}

namespace args_detail
{
inline int                g_argc = 0;
inline char const* const* g_argv = nullptr;
} // namespace args_detail

void args_capture(int argc, char const* const* argv) {
    args_detail::g_argc = argc;
    args_detail::g_argv = argv;
}

auto args_argc_argv() -> ArgcArgv {
    if (args_detail::g_argv == nullptr)
        return { __argc, reinterpret_cast<char const* const*>(__argv) };
    return { args_detail::g_argc, args_detail::g_argv };
}

} // namespace rstd::sys::pal::windows

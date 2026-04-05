module;
#include <rstd/macro.hpp>
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
    // Windows GetEnvironmentVariableA needs a buffer; for simplicity
    // use the CRT getenv which is available via stdlib.h
    return ::getenv(name);
}

export auto setenv_internal(const char* name, const char* value) -> bool {
    return libc::SetEnvironmentVariableA(name, value) != 0;
}

export auto unsetenv_internal(const char* name) -> bool {
    return libc::SetEnvironmentVariableA(name, nullptr) != 0;
}

} // namespace rstd::sys::pal::windows
#endif

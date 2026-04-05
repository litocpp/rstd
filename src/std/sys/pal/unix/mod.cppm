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

} // namespace rstd::sys::pal::unix
#endif

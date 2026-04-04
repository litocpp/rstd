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
}
} // namespace rstd::sys::pal::windows
#endif

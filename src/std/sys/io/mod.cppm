module;
#include <rstd/macro.hpp>
#if RSTD_OS_UNIX
#  include <errno.h>
#endif
export module rstd:sys.io;
export import rstd.core;

#if RSTD_OS_WINDOWS
import :sys.libc.windows;
#endif

namespace rstd::sys::io
{

export using RawOsError = i32;

export [[gnu::always_inline]] inline
auto last_os_error() noexcept -> RawOsError {
#if RSTD_OS_UNIX
    return errno;
#elif RSTD_OS_WINDOWS
    return static_cast<RawOsError>(libc::GetLastError());
#else
    return 0;
#endif
}

} // namespace rstd::sys::io

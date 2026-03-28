module;
#include <rstd/macro.hpp>
#if RSTD_OS_UNIX
#  include <errno.h>
#endif
#if RSTD_OS_WINDOWS
#  include <windows.h>
#endif
export module rstd:sys.io;
export import rstd.core;

namespace rstd::sys::io
{

export using RawOsError = i32;

export [[gnu::always_inline]] inline
auto last_os_error() noexcept -> RawOsError {
#if RSTD_OS_UNIX
    return errno;
#elif RSTD_OS_WINDOWS
    return static_cast<RawOsError>(GetLastError());
#else
    return 0;
#endif
}

} // namespace rstd::sys::io

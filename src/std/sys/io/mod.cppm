module;
#include <rstd/macro.hpp>
export module rstd:sys.io;
export import rstd.core;
import :sys.libc;

namespace rstd::sys::io
{

namespace libc = rstd::sys::libc;

export using RawOsError = i32;

export [[gnu::always_inline]]
inline auto last_os_error() noexcept -> RawOsError {
#if RSTD_OS_UNIX
    return libc::get_errno();
#elif RSTD_OS_WINDOWS
    return static_cast<RawOsError>(libc::GetLastError());
#else
    return 0;
#endif
}

} // namespace rstd::sys::io

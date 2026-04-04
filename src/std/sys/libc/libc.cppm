module;
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <rstd/macro.hpp>

export module rstd:sys.libc.std;

export namespace rstd::sys::libc
{
using ::timespec;


using ::abort;

// Memory management
using ::malloc;
using ::free;
using ::realloc;
using ::calloc;

// Utilities
using ::memcpy;
using ::memset;

// Types
using ::max_align_t;

#undef errno
[[gnu::always_inline]]
inline auto errno() noexcept -> int {
#if defined(RSTD_OS_LINUX)
    return *__errno_location();
#elif defined(RSTD_OS_WINDOWS)
    return *_errno();
#elif defined(RSTD_OS_APPLE)
    return *__error();
#else
#    error "rstd: unsupported platform for errno()"
#endif
};

} // namespace rstd::sys::libc

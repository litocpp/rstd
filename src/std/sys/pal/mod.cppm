module;
#include <rstd/macro.hpp>
export module rstd:sys.pal;

export import :sys.pal.unix;
export import :sys.pal.windows;

export namespace rstd::sys::pal
{
#if RSTD_OS_UNIX
namespace futex = pal::unix::futex;
using unix::Mutex;
using unix::Condvar;
using unix::Instant;
using unix::SystemTime;
using unix::abort_internal;
using unix::exit_internal;
using unix::getpid_internal;
using unix::getenv_internal;
using unix::setenv_internal;
using unix::unsetenv_internal;

#elif RSTD_OS_WINDOWS

namespace futex = pal::windows::futex;
using windows::Mutex;
using windows::Condvar;
using windows::Instant;
using windows::SystemTime;
using windows::abort_internal;
using windows::exit_internal;
using windows::getpid_internal;
using windows::getenv_internal;
using windows::setenv_internal;
using windows::unsetenv_internal;
#endif

} // namespace rstd::sys::pal

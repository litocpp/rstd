module;
#include <rstd/macro.hpp>
export module rstd:sys.pal;

#if RSTD_OS_UNIX
import :sys.pal.unix;
import :sys.pal.unix.futex;
#elif RSTD_OS_WINDOWS
import :sys.pal.windows;
import :sys.pal.windows.futex;
#endif

export namespace rstd::sys::pal
{
#if RSTD_OS_UNIX
namespace backend = unix;
#elif RSTD_OS_WINDOWS
namespace backend = windows;
#endif

namespace futex
{
using backend::futex::Duration;
using backend::futex::Primitive;
using backend::futex::Futex;
using backend::futex::SmallPrimitive;
using backend::futex::SmallFutex;
using backend::futex::futex_wait;
using backend::futex::futex_wake;
using backend::futex::futex_wake_all;
} // namespace futex

using backend::Mutex;
using backend::Condvar;
using backend::Instant;
using backend::SystemTime;
using backend::local_offset_at_unix_time;
using backend::abort_internal;
using backend::exit_internal;
using backend::getpid_internal;
using backend::getenv_internal;
using backend::setenv_internal;
using backend::unsetenv_internal;
using backend::ArgcArgv;
using backend::args_capture;
using backend::args_argc_argv;

} // namespace rstd::sys::pal

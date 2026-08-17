module;
#include <rstd/macro.hpp>
export module rstd:sys.pal;

#if RSTD_OS_UNIX
import :sys.pal.unix;
#elif RSTD_OS_WINDOWS
import :sys.pal.windows;
#endif

export namespace rstd::sys::pal
{
#if RSTD_OS_UNIX
namespace backend = unix;
#elif RSTD_OS_WINDOWS
namespace backend = windows;
#endif

namespace futex = backend::futex;
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

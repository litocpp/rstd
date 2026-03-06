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
using unix::abort_internal;

#elif RSTD_OS_WINDOWS

namespace futex = pal::windows::futex;
using windows::Mutex;
#endif

} // namespace rstd::sys::pal

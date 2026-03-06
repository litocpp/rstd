module;
#include <rstd/macro.hpp>
export module rstd:sys.thread;

#if RSTD_OS_LINUX
export import :sys.thread.unix;
#endif

namespace rstd::sys::thread
{
#if RSTD_OS_LINUX
using unix::Thread;
#endif
} // namespace rstd::sys::thread

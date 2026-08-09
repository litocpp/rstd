module;
#include <rstd/macro.hpp>
export module rstd:sys.thread;

#if RSTD_OS_LINUX
export import :sys.thread.unix;
#elif RSTD_OS_WINDOWS
export import :sys.thread.windows;
#endif

namespace rstd::sys::thread
{
#if RSTD_OS_LINUX
using unix::Thread;
inline auto available_parallelism() -> Option<usize> {
    return unix::Thread::available_parallelism();
}
#elif RSTD_OS_WINDOWS
using windows::Thread;
inline auto available_parallelism() -> Option<usize> {
    return windows::Thread::available_parallelism();
}
#endif
} // namespace rstd::sys::thread

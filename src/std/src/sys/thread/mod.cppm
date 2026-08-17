module;
#include <rstd/macro.hpp>
export module rstd:sys.thread;

#if RSTD_OS_UNIX
export import :sys.thread.unix;
namespace rstd::sys::thread
{
namespace backend = unix;
}
#elif RSTD_OS_WINDOWS
export import :sys.thread.windows;
namespace rstd::sys::thread
{
namespace backend = windows;
}
#endif

namespace rstd::sys::thread
{
using backend::Thread;
inline auto available_parallelism() -> Option<usize> {
    return backend::Thread::available_parallelism();
}
} // namespace rstd::sys::thread

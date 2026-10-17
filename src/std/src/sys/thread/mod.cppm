module;
#include <rstd/macro.hpp>
export module rstd.sys.thread;

#if RSTD_OS_UNIX
export import :unix;
namespace rstd::sys::thread
{
namespace backend = unix;
}
#elif RSTD_OS_WINDOWS
export import :windows;
namespace rstd::sys::thread
{
namespace backend = windows;
}
#endif

namespace rstd::sys::thread
{
export using backend::Thread;
export auto available_parallelism() -> Option<usize>;
} // namespace rstd::sys::thread

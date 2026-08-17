module;
#include <rstd/macro.hpp>
export module rstd:sys.sync.mutex;
#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
export import :sys.sync.mutex.futex;
namespace rstd::sys::sync::mutex
{
namespace backend = futex;
}
#else
export import :sys.sync.mutex.pthread;
namespace rstd::sys::sync::mutex
{
namespace backend = pthread;
}
#endif

namespace rstd::sys::sync::mutex
{
export using backend::Mutex;
} // namespace rstd::sys::sync::mutex

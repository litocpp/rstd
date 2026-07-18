module;
#include <rstd/macro.hpp>
export module rstd:sys.sync.mutex;
export import :sys.sync.mutex.futex;
export import :sys.sync.mutex.pthread;

namespace rstd::sys::sync::mutex
{
#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
export using mutex::futex::Mutex;
#else
export using mutex::pthread::Mutex;
#endif
} // namespace rstd::sys::sync::mutex

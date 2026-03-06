module;
#include <rstd/macro.hpp>
export module rstd:sys.sync.thread_parking;

#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
export import :sys.sync.thread_parking.futex;
#else
export import :sys.sync.thread_parking.pthread;
#endif

namespace rstd::sys::sync::thread_parking
{
#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
export using Parker = futex::Parker;
#else
export using Parker = pthread::Parker;
#endif

} // namespace rstd::sys::sync::thread_parking

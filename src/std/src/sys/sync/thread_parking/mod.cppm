module;
#include <rstd/macro.hpp>
export module rstd:sys.sync.thread_parking;

#if RSTD_OS_LINUX || RSTD_OS_WINDOWS
import :sys.sync.thread_parking.futex;
namespace rstd::sys::sync::thread_parking
{
namespace backend = futex;
}
#else
import :sys.sync.thread_parking.pthread;
namespace rstd::sys::sync::thread_parking
{
namespace backend = pthread;
}
#endif

namespace rstd::sys::sync::thread_parking
{
export using Parker = backend::Parker;

} // namespace rstd::sys::sync::thread_parking

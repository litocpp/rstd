module;
#include <rstd/macro.hpp>
export module rstd.sys.sync.thread_parking;

#if RSTD_VENDOR_APPLE
export import :darwin;
namespace rstd::sys::sync::thread_parking
{
namespace backend = darwin;
}
#elif RSTD_OS_LINUX || RSTD_OS_WINDOWS
export import :futex;
namespace rstd::sys::sync::thread_parking
{
namespace backend = futex;
}
#else
export import :pthread;
namespace rstd::sys::sync::thread_parking
{
namespace backend = pthread;
}
#endif

namespace rstd::sys::sync::thread_parking
{
export using Parker = backend::Parker;

} // namespace rstd::sys::sync::thread_parking

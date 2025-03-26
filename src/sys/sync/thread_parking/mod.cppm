export module rstd.sys:sync.thread_parking;

#if defined(__linux__) || defined(_WIN32)
import :sync.thread_parking.futex;
#else
import :sync.thread_parking.pthread;
#endif

namespace rstd::sync::thread_parking
{
#if defined(__linux__) || defined(_WIN32)
export using Parker = futex::Parker;
#else
export using Parker = pthread::Parker;
#endif

} // namespace parking
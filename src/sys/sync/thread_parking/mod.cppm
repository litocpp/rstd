export module rstd.sys:sync.thread_parking;

#if defined(__linux__) || defined(_WIN32)
export import :sync.thread_parking.futex;
#else
export import :sync.thread_parking.pthread;
#endif

namespace rstd::sys::sync::thread_parking
{
#if defined(__linux__) || defined(_WIN32)
export using Parker = futex::Parker;
#else
export using Parker = pthread::Parker;
#endif

} // namespace rstd::sys::sync::thread_parking
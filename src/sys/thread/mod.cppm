export module rstd:sys.thread;

#ifdef __linux__
export import :sys.thread.unix;
#endif

namespace rstd::sys::thread
{
#ifdef __linux__
using unix::Thread;
#endif
} // namespace rstd::sys::thread
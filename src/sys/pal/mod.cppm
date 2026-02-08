export module rstd:sys.pal;

export import :sys.pal.unix;
export import :sys.pal.windows;

export namespace rstd::sys::pal
{
#ifdef __unix__
namespace futex = pal::unix::futex;
using unix::Mutex;
using unix::Condvar;
#elif defined(_WIN32)
namespace futex = pal::windows::futex;
using windows::Mutex;
#endif

} // namespace rstd::sys::pal
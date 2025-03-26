export module rstd.sys:pal;

export import :pal.unix.futex;
export import :pal.windows.futex;

namespace rstd::pal
{

#ifdef __unix__
using namespace rstd::pal::unix;
#elif defined(_WIN32)
using namespace rstd::pal::windows;
#endif

} // namespace rstd::pal
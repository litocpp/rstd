export module rstd.sys:pal;

export import :pal.unix;
export import :pal.windows;

namespace rstd::sys::pal
{

#ifdef __unix__
using namespace pal::unix;
#elif defined(_WIN32)
using namespace pal::windows;
#endif

} // namespace rstd::sys::pal
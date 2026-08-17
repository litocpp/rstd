module;
#include <rstd/macro.hpp>

export module rstd:sys.pal.poll;
export import :sys.pal.poll.types;
#if RSTD_OS_LINUX
import :sys.pal.linux.poll;
#elif RSTD_OS_WINDOWS
import :sys.pal.windows.poll;
#endif

export namespace rstd::sys::pal::poll
{

#if RSTD_OS_LINUX
namespace backend = rstd::sys::pal::linux::poll;
#elif RSTD_OS_WINDOWS
namespace backend = rstd::sys::pal::windows::poll;
#endif

using backend::Poller;
using backend::PollWake;
using backend::PollInit;

} // namespace rstd::sys::pal::poll

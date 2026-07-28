module;
#include <rstd/macro.hpp>

export module rstd:sys.pal.poll;
export import :sys.pal.poll.types;
export import :sys.pal.linux.poll;
export import :sys.pal.windows.poll;

export namespace rstd::sys::pal::poll
{

#if RSTD_OS_LINUX
using Poller   = rstd::sys::pal::linux::poll::Poller;
using PollWake = rstd::sys::pal::linux::poll::PollWake;
using PollInit = rstd::sys::pal::linux::poll::PollInit;
#elif RSTD_OS_WINDOWS
using Poller   = rstd::sys::pal::windows::poll::Poller;
using PollWake = rstd::sys::pal::windows::poll::PollWake;
using PollInit = rstd::sys::pal::windows::poll::PollInit;
#endif

} // namespace rstd::sys::pal::poll

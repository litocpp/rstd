export module rstd.thread.thread:current;
export import :types;

namespace rstd::thread
{
export auto try_current() -> Option<Thread>;
export auto set_current(Thread thread) -> Result<empty, Thread>;
export auto current() -> Thread;
export auto current_id() -> ThreadId;
} // namespace rstd::thread

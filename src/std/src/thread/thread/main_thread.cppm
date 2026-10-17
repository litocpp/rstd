export module rstd.thread.thread:main_thread;
export import :id;

namespace rstd::thread::main_thread
{
export auto get() -> Option<ThreadId>;
export void set(ThreadId id);
} // namespace rstd::thread::main_thread

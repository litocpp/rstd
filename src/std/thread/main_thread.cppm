export module rstd:thread.main_thread;
export import :thread.id;

using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using rstd::thread::ThreadId;

namespace rstd::thread::main_thread
{

static auto MAIN { Atomic<u64> { 0 } };

export auto get() -> Option<ThreadId> { return ThreadId::from_u64(MAIN.load(Ordering::Relaxed)); }

export void set(ThreadId id) { MAIN.store(id.as_u64().get(), Ordering::Relaxed); }

} // namespace rstd::thread
export module rstd:thread.main_thread;
export import :thread.id;

using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using rstd::thread::ThreadId;

namespace rstd::thread::main_thread
{

static auto MAIN { Atomic<u64> { 0 } };

/// Returns the ThreadId of the main thread, or None if not yet set.
export auto get() -> Option<ThreadId> { return ThreadId::from_u64(MAIN.load(Ordering::Relaxed)); }

/// Stores the given ThreadId as the main thread identifier.
/// \param id The ThreadId to record as the main thread.
export void set(ThreadId id) { MAIN.store(id.as_u64().get(), Ordering::Relaxed); }

} // namespace rstd::thread
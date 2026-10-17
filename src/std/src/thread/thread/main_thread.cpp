module rstd.thread.thread;
import :main_thread;

using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using rstd::thread::ThreadId;

namespace rstd::thread::main_thread
{

static auto MAIN { Atomic<u64> { u64 {} } };

/// Returns the ThreadId of the main thread, or None if not yet set.
auto get() -> Option<ThreadId> {
    return ThreadId::from_u64(MAIN.load(Ordering::Relaxed));
}

/// Stores the given ThreadId as the main thread identifier.
/// \param id The ThreadId to record as the main thread.
void set(ThreadId id) {
    MAIN.store(id.as_u64().get(), Ordering::Relaxed);
}

} // namespace rstd::thread::main_thread

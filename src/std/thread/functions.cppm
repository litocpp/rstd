module;
#include <rstd/macro.hpp>
export module rstd:thread.functions;
export import :thread.builder;
export import :thread.thread;
export import :thread.join_handle;
export import :thread.current;
export import :sys;
export import rstd.core;

namespace rstd::thread
{

/// Spawns a new thread, returning a JoinHandle for it.
/// \tparam F The callable type for the thread entry point.
/// \param f The closure or function to execute in the new thread.
/// \return A JoinHandle on success, or an I/O error on failure.
export template<typename F>
auto spawn(F&& f) -> io::Result<JoinHandle<mtp::invoke_result_t<F>>> {
    return builder::Builder::make().spawn(rstd::forward<F>(f));
}

/// Puts the current thread to sleep for at least the specified duration.
/// \param dur The minimum duration to sleep.
export inline void sleep(rstd::time::Duration dur) {
    sys::thread::Thread::sleep(dur);
}

/// Cooperatively gives up a timeslice to the OS scheduler.
export inline void yield_now() {
    sys::thread::Thread::yield_now();
}

/// Blocks the current thread until its token is made available via `unpark`.
export inline void park() {
    current().park();
}

/// Blocks the current thread for at most the specified duration, or until unparked.
/// \param timeout The maximum duration to block.
export inline void park_timeout(rstd::time::Duration timeout) {
    current().park_timeout(timeout);
}

/// Atomically makes the given thread's token available, waking it if parked.
/// \param thread The thread to unpark.
export inline void unpark(const Thread& thread) {
    thread.unpark();
}

} // namespace rstd::thread

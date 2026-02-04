export module rstd.thread:scoped;
export import :thread;
export import rstd.core;

using rstd::sync::atomic::Atomic;

namespace rstd::thread::scoped
{

struct ScopeData {
    mutable Atomic<usize> num_running_threads;
    mutable Atomic<bool>  a_thread_panicked;
    Thread                main_thread;

    using Self = ScopeData;

    void increment_num_running_threads(this Self const& self) {
        // We check for 'overflow' with usize::MAX / 2, to make sure there's no
        // chance it overflows to 0, which would result in unsoundness.
        if (self.num_running_threads.fetch_add(1, Ordering::Relaxed) >
            rstd::numeric_limits<usize>::max() / 2) {
            // This can only reasonably happen by mem::forget()'ing a lot of ScopedJoinHandles.
            self.overflow();
        }
    }

    [[gnu::cold]]
    void overflow(this Self const& self) {
        self.decrement_num_running_threads(false);
        panic("too many running threads in thread scope");
    }

    void decrement_num_running_threads(this Self const& self, bool panic) {
        if (panic) {
            self.a_thread_panicked.store(true, Ordering::Relaxed);
        }
        if (self.num_running_threads.fetch_sub(1, Ordering::Release) == 1) {
            self.main_thread.unpark();
        }
    }
};

} // namespace rstd::thread::scpoed
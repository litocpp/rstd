export module rstd:thread.scoped;
export import :thread.thread;
export import rstd.core;

using rstd::sync::atomic::Atomic;

namespace rstd::thread::scoped
{

struct ScopeData {
    mutable Atomic<usize> num_running_threads;
    mutable Atomic<bool>  a_thread_panicked;
    Thread                main_thread;

    using Self = ScopeData;

    void increment_num_running_threads() const {
        // We check for 'overflow' with usize::MAX / 2, to make sure there's no
        // chance it overflows to 0, which would result in unsoundness.
        if (num_running_threads.fetch_add(1, Ordering::Relaxed) >
            rstd::numeric_limits<usize>::max() / 2) {
            // This can only reasonably happen by mem::forget()'ing a lot of ScopedJoinHandles.
            overflow();
        }
    }

    [[gnu::cold]]
    void overflow() const {
        decrement_num_running_threads(false);
        panic{"too many running threads in thread scope"};
    }

    void decrement_num_running_threads(bool panic) const {
        if (panic) {
            a_thread_panicked.store(true, Ordering::Relaxed);
        }
        if (num_running_threads.fetch_sub(1, Ordering::Release) == 1) {
            main_thread.unpark();
        }
    }
};

} // namespace rstd::thread::scoped
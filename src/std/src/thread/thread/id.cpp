module rstd.thread.thread;
import :id;

namespace rstd::thread
{
ThreadId ThreadId::make() {
    auto exhausted = []() {
        panic { "failed to generate unique thread ID: bitspace exhausted" };
    };

    using rstd::sync::atomic::Atomic;
    using rstd::sync::atomic::Ordering;

    static auto COUNTER = Atomic<u64>(u64 {});
    auto        last    = COUNTER.load(Ordering::Relaxed);
    for (;;) {
        if (auto id_ = last.checked_add(u64(1))) {
            const auto id = id_.unwrap_unchecked();
            if (COUNTER.compare_exchange_weak(last, id, Ordering::Relaxed, Ordering::Relaxed)) {
                return { NonZero<u64>::make(id).unwrap() };
            } else {
                last = id;
            }
        } else {
            exhausted();
        }
    }
}
} // namespace rstd::thread

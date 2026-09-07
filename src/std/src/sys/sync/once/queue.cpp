module rstd;
import :sys.sync.once.queue;
import :thread.functions;

using rstd::sync::atomic::Ordering;

namespace rstd::sys::sync::once::queue
{

inline constexpr rstd::uintptr_t COMPLETE {};
inline constexpr rstd::uintptr_t RUNNING    = 1;
inline constexpr rstd::uintptr_t INCOMPLETE = 3;
inline constexpr rstd::uintptr_t STATE_MASK = 0b11;
inline constexpr rstd::uintptr_t QUEUE_MASK = ~STATE_MASK;

struct Waiter {
    rstd::thread::Thread             thread;
    rstd::sync::atomic::Atomic<bool> signaled;
    Waiter*                          next;
};

auto state_of(rstd::uintptr_t value) noexcept -> rstd::uintptr_t {
    return value & STATE_MASK;
}

auto queue_of(rstd::uintptr_t value) noexcept -> Waiter* {
    return reinterpret_cast<Waiter*>(value & QUEUE_MASK);
}

auto wait(rstd::sync::atomic::Atomic<rstd::uintptr_t>& state_and_queue, rstd::uintptr_t current)
    -> rstd::uintptr_t {
    auto waiter = Waiter { .thread = rstd::thread::current(), .signaled = false, .next = nullptr };

    while (true) {
        auto const state = state_of(current);
        if (state == COMPLETE) return current;

        waiter.next        = queue_of(current);
        auto const desired = reinterpret_cast<rstd::uintptr_t>(&waiter) | state;
        if (! state_and_queue.compare_exchange_weak(
                current, desired, Ordering::Release, Ordering::Acquire)) {
            continue;
        }

        while (! waiter.signaled.load(Ordering::Acquire)) rstd::thread::park();
        return state_and_queue.load(Ordering::Acquire);
    }
}

auto Once::is_completed() const noexcept -> bool {
    return state_of(state_and_queue.load(Ordering::Acquire)) == COMPLETE;
}

void Once::wait() const {
    auto current = state_and_queue.load(Ordering::Acquire);
    while (state_of(current) != COMPLETE) current = queue::wait(state_and_queue, current);
}

void Once::call(void* context, Callback callback) const {
    auto current = state_and_queue.load(Ordering::Acquire);
    while (true) {
        auto const state = state_of(current);
        if (state == COMPLETE) return;

        if (state == INCOMPLETE) {
            auto const desired = (current & QUEUE_MASK) | RUNNING;
            if (! state_and_queue.compare_exchange_weak(
                    current, desired, Ordering::Acquire, Ordering::Acquire)) {
                continue;
            }

            callback(context);

            auto queue = queue_of(state_and_queue.exchange(COMPLETE, Ordering::AcqRel));
            while (queue != nullptr) {
                auto* next   = queue->next;
                auto  thread = queue->thread.clone();
                queue->signaled.store(true, Ordering::Release);
                thread.unpark();
                queue = next;
            }
            return;
        }

        if (state != RUNNING) rstd::panic { "invalid Once state" };
        current = queue::wait(state_and_queue, current);
    }
}

auto Once::state() & noexcept -> ExclusiveState {
    auto const state = state_of(state_and_queue.load(Ordering::Relaxed));
    if (state == COMPLETE) return ExclusiveState::Complete;
    return ExclusiveState::Incomplete;
}

void Once::set_state(ExclusiveState state) & noexcept {
    auto const value = state == ExclusiveState::Complete ? COMPLETE : INCOMPLETE;
    state_and_queue.store(value, Ordering::Relaxed);
}

} // namespace rstd::sys::sync::once::queue

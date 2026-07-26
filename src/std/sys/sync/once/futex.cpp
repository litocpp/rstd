module rstd;
import :sys.pal;
import :sys.sync.once.futex;

namespace rstd::sys::sync::once::futex
{
auto Once::is_completed() const noexcept -> bool {
    return state_and_queued.load(rstd::sync::atomic::Ordering::Acquire) == COMPLETE;
}

void Once::wait() const {
    Primitive state_and_queued = this->state_and_queued.load(rstd::sync::atomic::Ordering::Acquire);
    while (true) {
        Primitive const state  = state_and_queued & STATE_MASK;
        bool const      queued = (state_and_queued & QUEUED) != Primitive {};
        if (state == COMPLETE) return;

        if (! queued) {
            auto expected = state_and_queued;
            auto desired  = state_and_queued | QUEUED;
            if (! this->state_and_queued.compare_exchange_weak(
                    expected,
                    desired,
                    rstd::sync::atomic::Ordering::Relaxed,
                    rstd::sync::atomic::Ordering::Acquire)) {
                state_and_queued = expected;
                continue;
            }
            state_and_queued = desired;
        }

        pal::futex::futex_wait(&this->state_and_queued, state_and_queued, rstd::None());
        state_and_queued = this->state_and_queued.load(rstd::sync::atomic::Ordering::Acquire);
    }
}

void Once::call(void* context, Callback callback) const {
    Primitive state_and_queued = this->state_and_queued.load(rstd::sync::atomic::Ordering::Acquire);
    while (true) {
        Primitive const state  = state_and_queued & STATE_MASK;
        bool const      queued = (state_and_queued & QUEUED) != Primitive {};

        if (state == COMPLETE) return;

        if (state == INCOMPLETE) {
            auto expected = state_and_queued;
            auto next     = RUNNING | (queued ? QUEUED : Primitive {});
            if (this->state_and_queued.compare_exchange_weak(
                    expected,
                    next,
                    rstd::sync::atomic::Ordering::Acquire,
                    rstd::sync::atomic::Ordering::Acquire)) {
                callback(context);
                if ((this->state_and_queued.exchange(COMPLETE,
                                                     rstd::sync::atomic::Ordering::Release) &
                     QUEUED) != Primitive {}) {
                    pal::futex::futex_wake_all(&this->state_and_queued);
                }
                return;
            }
            state_and_queued = expected;
            continue;
        }

        if (! queued) {
            auto expected = state_and_queued;
            auto desired  = state_and_queued | QUEUED;
            if (! this->state_and_queued.compare_exchange_weak(
                    expected,
                    desired,
                    rstd::sync::atomic::Ordering::Relaxed,
                    rstd::sync::atomic::Ordering::Acquire)) {
                state_and_queued = expected;
                continue;
            }
            state_and_queued = desired;
        }

        pal::futex::futex_wait(&this->state_and_queued, state_and_queued, rstd::None());
        state_and_queued = this->state_and_queued.load(rstd::sync::atomic::Ordering::Acquire);
    }
}

auto Once::state() & noexcept -> ExclusiveState {
    auto const state = state_and_queued.load(rstd::sync::atomic::Ordering::Relaxed);
    if (state == COMPLETE) return ExclusiveState::Complete;
    return ExclusiveState::Incomplete;
}

void Once::set_state(ExclusiveState state) & noexcept {
    auto primitive = INCOMPLETE;
    if (state == ExclusiveState::Complete) primitive = COMPLETE;
    state_and_queued.store(primitive, rstd::sync::atomic::Ordering::Relaxed);
}
} // namespace rstd::sys::sync::once::futex

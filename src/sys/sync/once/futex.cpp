module rstd.sys;
import :sync.once.futex;

namespace rstd::sys::sync::once::futex
{
CompletionGuard::CompletionGuard(Futex* state_and_queued, Primitive set_state_on_drop_to)
    : state_and_queued(state_and_queued), set_state_on_drop_to(set_state_on_drop_to) {}

CompletionGuard::~CompletionGuard() {
    if (state_and_queued->exchange(set_state_on_drop_to, rstd::memory_order::release) & QUEUED) {
        pal::futex::futex_wake_all(state_and_queued);
    }
}

Once::Once(): state_and_queued(INCOMPLETE) {}

bool Once::is_completed() const {
    return state_and_queued.load(rstd::memory_order::acquire) == COMPLETE;
}

void Once::wait(bool ignore_poisoning) {
    Primitive state_and_queued = this->state_and_queued.load(rstd::memory_order::acquire);
    while (true) {
        Primitive state  = state_and_queued & STATE_MASK;
        bool      queued = state_and_queued & QUEUED;
        switch (state) {
        case COMPLETE: return;
        case POISONED:
            if (! ignore_poisoning) {
                panic("Once instance has previously been poisoned");
            }
            break;
        default:
            if (! queued) {
                state_and_queued += QUEUED;
                if (this->state_and_queued.compare_exchange_weak(state,
                                                                 state_and_queued,
                                                                 rstd::memory_order::relaxed,
                                                                 rstd::memory_order::acquire)) {
                    continue;
                }
            }
            pal::futex::futex_wait(&this->state_and_queued, state_and_queued, rstd::None());
            state_and_queued = this->state_and_queued.load(rstd::memory_order::acquire);
            break;
        }
    }
}

/*
void Once::call(bool ignore_poisoning, Dyn<FnMut, void(OnceState&)> f) {
    Primitive state_and_queued = this->state_and_queued.load(std::memory_order_acquire);
    while (true) {
        Primitive state  = state_and_queued & STATE_MASK;
        bool      queued = state_and_queued & QUEUED;
        switch (state) {
        case COMPLETE: return;
        case POISONED:
            if (! ignore_poisoning) {
                throw std::runtime_error("Once instance has previously been poisoned");
            }
            // fall through to INCOMPLETE
        case INCOMPLETE: {
            Primitive next = RUNNING + (queued ? QUEUED : 0);
            if (this->state_and_queued.compare_exchange_weak(
                    state_and_queued, next, std::memory_order_acquire, std::memory_order_acquire)) {
                CompletionGuard waiter_queue(&this->state_and_queued, POISONED);
                OnceState       f_state;
                f(f_state);
                waiter_queue.set_state_on_drop_to = f_state.set_state_to.load();
                return;
            }
            continue;
        }
        default:
            assert(state == RUNNING);
            if (! queued) {
                state_and_queued += QUEUED;
                if (this->state_and_queued.compare_exchange_weak(state,
                                                                 state_and_queued,
                                                                 std::memory_order_relaxed,
                                                                 std::memory_order_acquire)) {
                    continue;
                }
            }
            futex_wait(&this->state_and_queued, state_and_queued, nullptr);
            state_and_queued = this->state_and_queued.load(std::memory_order_acquire);
            break;
        }
    }
}
    */
} // namespace rstd::sys::sync::once
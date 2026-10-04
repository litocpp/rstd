export module rstd:async.mutex;
export import :async.forward;
import rstd.alloc;
import :sync;

using namespace rstd::prelude;
using rstd::sync::Arc;

namespace rstd::async
{

struct MutexWaiter {
    MutexWaiter*        previous {};
    MutexWaiter*        next {};
    Option<task::Waker> waker;
    bool                queued {};
    bool                granted {};
};

template<typename T>
struct MutexState {
    struct Queue {
        MutexWaiter* head {};
        MutexWaiter* tail {};
        bool         locked {};

        void unlink(MutexWaiter& waiter) {
            if (waiter.previous)
                waiter.previous->next = waiter.next;
            else
                head = waiter.next;
            if (waiter.next)
                waiter.next->previous = waiter.previous;
            else
                tail = waiter.previous;
            waiter.previous = nullptr;
            waiter.next     = nullptr;
            waiter.queued   = false;
        }

        auto release() -> Option<task::Waker> {
            if (! head) {
                locked = false;
                return None();
            }
            auto& next = *head;
            unlink(next);
            next.granted = true;
            return next.waker.take();
        }
    };

    sync::Mutex<Queue> queue;
    T                  value;

    template<typename... Args>
    explicit MutexState(Args&&... args): queue(Queue {}), value(rstd::forward<Args>(args)...) {}

    auto acquire(MutexWaiter& waiter, task::Context& cx) -> bool {
        Option<task::Waker> discarded;
        auto                q = queue.lock().unwrap_unchecked();
        if (waiter.granted) return true;
        if (! q->locked) {
            q->locked      = true;
            waiter.granted = true;
            return true;
        }
        if (waiter.waker.is_none() || ! waiter.waker->will_wake(cx.waker())) {
            discarded    = waiter.waker.take();
            waiter.waker = Some(cx.waker().clone());
        }
        if (! waiter.queued) {
            waiter.previous = q->tail;
            if (q->tail)
                q->tail->next = &waiter;
            else
                q->head = &waiter;
            q->tail       = &waiter;
            waiter.queued = true;
        }
        return false;
    }

    void release() {
        Option<task::Waker> wake;
        {
            auto q = queue.lock().unwrap_unchecked();
            wake   = q->release();
        }
        if (wake.is_some()) rstd::move(*wake).wake();
    }

    void cancel(MutexWaiter& waiter) {
        Option<task::Waker> wake;
        Option<task::Waker> discarded;
        {
            auto q = queue.lock().unwrap_unchecked();
            if (waiter.queued) q->unlink(waiter);
            if (waiter.granted) wake = q->release();
            discarded = waiter.waker.take();
        }
        if (wake.is_some()) rstd::move(*wake).wake();
    }
};

export template<typename T>
class Mutex;
export template<typename T>
class MutexLock;

/// An owned guard; it may cross suspension points and release on another thread.
export template<typename T>
class MutexGuard {
    Arc<MutexState<T>> state;
    explicit MutexGuard(Arc<MutexState<T>> state): state(rstd::move(state)) {}
    friend class MutexLock<T>;

public:
    MutexGuard(const MutexGuard&)                    = delete;
    auto operator=(const MutexGuard&) -> MutexGuard& = delete;
    MutexGuard(MutexGuard&&) noexcept                = default;
    auto operator=(MutexGuard&& other) noexcept -> MutexGuard& {
        if (this != &other) {
            if (state) state->release();
            state = rstd::move(other.state);
        }
        return *this;
    }
    ~MutexGuard() {
        if (state) state->release();
    }

    auto operator*() -> T& { return state->value; }
    auto operator*() const -> const T& { return state->value; }
    auto operator->() -> T* { return &state->value; }
    auto operator->() const -> const T* { return &state->value; }
};

export template<typename T>
class MutexLock {
    Arc<MutexState<T>> state;
    Arc<MutexWaiter>   waiter;
    explicit MutexLock(Arc<MutexState<T>> state)
        : state(rstd::move(state)), waiter(Arc<MutexWaiter>::make()) {}
    friend class Mutex<T>;

    void cancel() {
        if (state) state->cancel(*waiter);
    }

public:
    using Output                                   = MutexGuard<T>;
    MutexLock(const MutexLock&)                    = delete;
    auto operator=(const MutexLock&) -> MutexLock& = delete;
    MutexLock(MutexLock&&) noexcept                = default;
    auto operator=(MutexLock&& other) noexcept -> MutexLock& {
        if (this != &other) {
            cancel();
            state  = rstd::move(other.state);
            waiter = rstd::move(other.waiter);
        }
        return *this;
    }
    ~MutexLock() { cancel(); }

    auto poll(mut_ref<MutexLock> self, task::Context& cx) -> task::Poll<Output> {
        if (! self->state) rstd::panic { "async::MutexLock polled after completion" };
        if (! self->state->acquire(*self->waiter, cx)) return task::Poll<Output>::Pending();
        return task::Poll<Output>::Ready(Output { rstd::move(self->state) });
    }
};

/// FIFO by first poll. Dropping a waiter cancels its queue position or reserved grant.
export template<typename T>
class Mutex {
    Arc<MutexState<T>> state;
    explicit Mutex(Arc<MutexState<T>> state): state(rstd::move(state)) {}

public:
    template<typename... Args>
    explicit Mutex(Args&&... args): state(Arc<MutexState<T>>::make(rstd::forward<Args>(args)...)) {}
    Mutex(const Mutex&)                        = delete;
    auto operator=(const Mutex&) -> Mutex&     = delete;
    Mutex(Mutex&&) noexcept                    = default;
    auto operator=(Mutex&&) noexcept -> Mutex& = default;

    auto clone() const -> Mutex { return Mutex { state.clone() }; }
    auto lock() const -> MutexLock<T> { return MutexLock<T> { state.clone() }; }
};

} // namespace rstd::async

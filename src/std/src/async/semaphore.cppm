export module rstd:async.semaphore;
export import :async.forward;
export import rstd.error;
import rstd.alloc;
import :sync;

using namespace rstd::prelude;
using rstd::array;
using rstd::sync::Arc;

namespace rstd::async
{

export struct AcquireError {
    bool operator==(const AcquireError&) const = default;
};

export enum class TryAcquireError { Closed, NoPermits };

struct SemaphoreWaiter {
    SemaphoreWaiter*    previous {};
    SemaphoreWaiter*    next {};
    Option<task::Waker> waker;
    const usize         requested;
    usize               remaining;
    bool                queued {};

    explicit SemaphoreWaiter(usize count): requested(count), remaining(count) {}
};

struct SemaphoreWakes {
    array<Option<task::Waker>, 32> entries {};
    usize                          count {};

    auto full() const -> bool { return count == entries.len(); }

    void push(task::Waker waker) { entries[count++] = Some(rstd::move(waker)); }

    void wake_all() {
        for (usize index {}; index < count; ++index) {
            auto waker = entries[index].take();
            rstd::move(*waker).wake();
        }
        count = usize();
    }
};

struct SemaphoreState {
    struct Queue {
        SemaphoreWaiter* head {};
        SemaphoreWaiter* tail {};
        usize            available;
        bool             closed {};

        explicit Queue(usize permits): available(permits) {}

        void unlink(SemaphoreWaiter& waiter) {
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

        void push(SemaphoreWaiter& waiter) {
            waiter.previous = tail;
            if (tail)
                tail->next = &waiter;
            else
                head = &waiter;
            tail          = &waiter;
            waiter.queued = true;
        }

        void release(usize& permits, SemaphoreWakes& wakes) {
            while (! closed && head && permits != usize() && ! wakes.full()) {
                auto& waiter   = *head;
                auto  assigned = rstd::cmp::min(waiter.remaining, permits);
                waiter.remaining -= assigned;
                permits -= assigned;
                if (waiter.remaining != usize()) break;
                unlink(waiter);
                auto waker = waiter.waker.take();
                if (waker.is_some()) wakes.push(rstd::move(*waker));
            }
            if (closed || ! head) {
                available += permits;
                permits = usize();
            }
        }
    };

    sync::Mutex<Queue> queue;

    explicit SemaphoreState(usize permits): queue(permits) {}

    auto poll(SemaphoreWaiter& waiter, task::Context& cx) -> Result<bool, AcquireError> {
        auto                replacement = Some(cx.waker().clone());
        Option<task::Waker> discarded;
        auto                q = queue.lock().unwrap_unchecked();
        if (q->closed) return Err(AcquireError {});
        if (waiter.remaining == usize()) return Ok(true);
        if (! waiter.queued && ! q->head) {
            auto assigned = rstd::cmp::min(q->available, waiter.remaining);
            q->available -= assigned;
            waiter.remaining -= assigned;
            if (waiter.remaining == usize()) return Ok(true);
        }
        if (waiter.waker.is_none() || ! waiter.waker->will_wake(cx.waker())) {
            discarded    = waiter.waker.take();
            waiter.waker = replacement.take();
        }
        if (! waiter.queued) q->push(waiter);
        return Ok(false);
    }

    auto try_acquire(usize permits) -> Result<empty, TryAcquireError> {
        auto q = queue.lock().unwrap_unchecked();
        if (q->closed) return Err(TryAcquireError::Closed);
        if ((permits != usize() && q->head) || q->available < permits)
            return Err(TryAcquireError::NoPermits);
        q->available -= permits;
        return Ok(empty {});
    }

    void release(usize permits) {
        // Keep the unassigned remainder out of available while waiters remain.
        while (permits != usize()) {
            SemaphoreWakes wakes;
            {
                auto q = queue.lock().unwrap_unchecked();
                q->release(permits, wakes);
            }
            wakes.wake_all();
        }
    }

    void cancel(SemaphoreWaiter& waiter) {
        usize               permits;
        Option<task::Waker> discarded;
        {
            auto q = queue.lock().unwrap_unchecked();
            if (waiter.queued) q->unlink(waiter);
            // A dequeued waiter can still own a complete reservation before its next poll.
            permits          = waiter.requested - waiter.remaining;
            waiter.remaining = waiter.requested;
            discarded        = waiter.waker.take();
        }
        release(permits);
    }

    void close() {
        for (;;) {
            SemaphoreWakes wakes;
            bool           empty;
            {
                auto q    = queue.lock().unwrap_unchecked();
                q->closed = true;
                while (q->head && ! wakes.full()) {
                    auto& waiter = *q->head;
                    q->unlink(waiter);
                    auto waker = waiter.waker.take();
                    if (waker.is_some()) wakes.push(rstd::move(*waker));
                }
                empty = q->head == nullptr;
            }
            wakes.wake_all();
            if (empty) return;
        }
    }

    auto available_permits() const -> usize {
        auto q = queue.lock().unwrap_unchecked();
        return q->available;
    }

    auto is_closed() const -> bool {
        auto q = queue.lock().unwrap_unchecked();
        return q->closed;
    }
};

export class Semaphore;
export class SemaphoreAcquire;

/// An owned permit; moving it transfers the obligation to return its permits.
export class OwnedSemaphorePermit {
    Arc<SemaphoreState> state_;
    usize               permits_;

    OwnedSemaphorePermit(Arc<SemaphoreState> state, usize permits)
        : state_(rstd::move(state)), permits_(permits) {}
    friend class SemaphoreAcquire;
    friend class Semaphore;

    void release() {
        auto state   = rstd::move(state_);
        auto permits = rstd::exchange(permits_, usize());
        if (state) state->release(permits);
    }

public:
    OwnedSemaphorePermit(const OwnedSemaphorePermit&)                    = delete;
    auto operator=(const OwnedSemaphorePermit&) -> OwnedSemaphorePermit& = delete;
    OwnedSemaphorePermit(OwnedSemaphorePermit&& other) noexcept
        : state_(rstd::move(other.state_)), permits_(rstd::exchange(other.permits_, usize())) {}
    auto operator=(OwnedSemaphorePermit&& other) noexcept -> OwnedSemaphorePermit& {
        if (this != &other) {
            release();
            state_   = rstd::move(other.state_);
            permits_ = rstd::exchange(other.permits_, usize());
        }
        return *this;
    }
    ~OwnedSemaphorePermit() { release(); }
};

export class SemaphoreAcquire {
    Arc<SemaphoreState>  state_;
    Arc<SemaphoreWaiter> waiter_;

    SemaphoreAcquire(Arc<SemaphoreState> state, usize permits)
        : state_(rstd::move(state)), waiter_(Arc<SemaphoreWaiter>::make(permits)) {}
    friend class Semaphore;

    void cancel() {
        auto state  = rstd::move(state_);
        auto waiter = rstd::move(waiter_);
        if (state) state->cancel(*waiter);
    }

public:
    using Output = Result<OwnedSemaphorePermit, AcquireError>;

    SemaphoreAcquire(const SemaphoreAcquire&)                    = delete;
    auto operator=(const SemaphoreAcquire&) -> SemaphoreAcquire& = delete;
    SemaphoreAcquire(SemaphoreAcquire&&) noexcept                = default;
    auto operator=(SemaphoreAcquire&& other) noexcept -> SemaphoreAcquire& {
        if (this != &other) {
            cancel();
            state_  = rstd::move(other.state_);
            waiter_ = rstd::move(other.waiter_);
        }
        return *this;
    }
    ~SemaphoreAcquire() { cancel(); }

    auto poll(mut_ref<SemaphoreAcquire> self, task::Context& cx) -> task::Poll<Output> {
        if (! self->state_)
            rstd::panic { "async::SemaphoreAcquire polled after completion or move" };
        auto acquired = self->state_->poll(*self->waiter_, cx);
        if (acquired.is_err()) {
            self->cancel();
            return task::Poll<Output>::Ready(Err(rstd::move(acquired).unwrap_err()));
        }
        if (! *acquired) return task::Poll<Output>::Pending();
        auto permit = OwnedSemaphorePermit(rstd::move(self->state_), self->waiter_->requested);
        self->waiter_.reset();
        return task::Poll<Output>::Ready(Ok(rstd::move(permit)));
    }
};

/// FIFO by first poll, including partial reservations for multi-permit waiters.
export class Semaphore {
    Arc<SemaphoreState> state_;
    explicit Semaphore(Arc<SemaphoreState> state): state_(rstd::move(state)) {}

public:
    explicit Semaphore(usize permits): state_(Arc<SemaphoreState>::make(permits)) {}
    Semaphore(const Semaphore&)                        = delete;
    auto operator=(const Semaphore&) -> Semaphore&     = delete;
    Semaphore(Semaphore&&) noexcept                    = default;
    auto operator=(Semaphore&&) noexcept -> Semaphore& = default;

    auto clone() const -> Semaphore { return Semaphore { state_.clone() }; }
    auto acquire_owned() const -> SemaphoreAcquire { return acquire_many_owned(usize(1)); }
    auto acquire_many_owned(usize permits) const -> SemaphoreAcquire {
        return SemaphoreAcquire { state_.clone(), permits };
    }
    auto try_acquire_owned() const -> Result<OwnedSemaphorePermit, TryAcquireError> {
        return try_acquire_many_owned(usize(1));
    }
    auto try_acquire_many_owned(usize permits) const
        -> Result<OwnedSemaphorePermit, TryAcquireError> {
        auto acquired = state_->try_acquire(permits);
        if (acquired.is_err()) return Err(rstd::move(acquired).unwrap_err());
        return Ok(OwnedSemaphorePermit { state_.clone(), permits });
    }
    auto available_permits() const -> usize { return state_->available_permits(); }
    void close() const { state_->close(); }
    auto is_closed() const -> bool { return state_->is_closed(); }
};

} // namespace rstd::async

export namespace rstd
{

template<>
struct Impl<fmt::Display, async::AcquireError> : ImplBase<async::AcquireError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "semaphore closed";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Debug, async::AcquireError> : ImplBase<async::AcquireError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        constexpr char message[] = "AcquireError";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<error::Error, async::AcquireError> : DefaultInImpl<error::Error, async::AcquireError> {
};

template<>
struct Impl<fmt::Display, async::TryAcquireError> : ImplBase<async::TryAcquireError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self() == async::TryAcquireError::Closed) {
            constexpr char message[] = "semaphore closed";
            return formatter.write_raw(message, sizeof(message) - 1);
        }
        constexpr char message[] = "no permits available";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<fmt::Debug, async::TryAcquireError> : ImplBase<async::TryAcquireError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        if (this->self() == async::TryAcquireError::Closed) {
            constexpr char message[] = "Closed";
            return formatter.write_raw(message, sizeof(message) - 1);
        }
        constexpr char message[] = "NoPermits";
        return formatter.write_raw(message, sizeof(message) - 1);
    }
};

template<>
struct Impl<error::Error, async::TryAcquireError>
    : DefaultInImpl<error::Error, async::TryAcquireError> {};

} // namespace rstd

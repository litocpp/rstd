module;
#include <rstd/macro.hpp>

export module rstd:async.notify;
export import :async.forward;
export import rstd.io;
import rstd.alloc;
import :sync;

using namespace rstd;

namespace rstd::async
{

export class NotifyHandle;
export class NotifyFuture;

struct NotifyWaiter {
    NotifyWaiter*       previous {};
    NotifyWaiter*       next {};
    Option<task::Waker> waker;
    bool                queued {};
    bool                granted {};
};

struct NotifyState {
    struct Queue {
        NotifyWaiter* head {};
        NotifyWaiter* tail {};
        bool          permit {};

        void unlink(NotifyWaiter& waiter) {
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

        auto notify() -> Option<task::Waker> {
            if (! head) {
                permit = true;
                return None();
            }
            auto& next = *head;
            unlink(next);
            next.granted = true;
            return next.waker.take();
        }
    };

    sync::Mutex<Queue> queue { Queue {} };

    auto notify() -> io::Result<empty> {
        Option<task::Waker> wake;
        {
            auto state = queue.lock().unwrap_unchecked();
            wake       = state->notify();
        }
        if (wake.is_some()) rstd::move(*wake).wake();
        return Ok(empty {});
    }

    auto poll(NotifyWaiter& waiter, task::Context& cx) -> bool {
        Option<task::Waker> discarded;
        auto                state = queue.lock().unwrap_unchecked();
        if (waiter.granted) {
            waiter.granted = false;
            return true;
        }
        if (state->permit) {
            state->permit = false;
            return true;
        }
        if (waiter.waker.is_none() || ! waiter.waker->will_wake(cx.waker())) {
            discarded    = waiter.waker.take();
            waiter.waker = Some(cx.waker().clone());
        }
        if (! waiter.queued) {
            waiter.previous = state->tail;
            if (state->tail)
                state->tail->next = &waiter;
            else
                state->head = &waiter;
            state->tail   = &waiter;
            waiter.queued = true;
        }
        return false;
    }

    void cancel(NotifyWaiter& waiter) {
        Option<task::Waker> wake;
        Option<task::Waker> discarded;
        {
            auto state = queue.lock().unwrap_unchecked();
            if (waiter.queued) state->unlink(waiter);
            if (waiter.granted) {
                waiter.granted = false;
                wake           = state->notify();
            }
            discarded = waiter.waker.take();
        }
        if (wake.is_some()) rstd::move(*wake).wake();
    }
};

export class Notify {
    sync::Arc<NotifyState> m_state;

    explicit Notify(sync::Arc<NotifyState> state): m_state(rstd::move(state)) {}

public:
    Notify(const Notify&)                        = delete;
    auto operator=(const Notify&) -> Notify&     = delete;
    Notify(Notify&&) noexcept                    = default;
    auto operator=(Notify&&) noexcept -> Notify& = default;
    ~Notify()                                    = default;

    auto clone() const -> Notify { return Notify { m_state.clone() }; }

    static auto make() -> io::Result<Notify> {
        return Ok(Notify { sync::Arc<NotifyState>::make() });
    }

    auto notifier() const -> NotifyHandle;
    auto notified() const -> NotifyFuture;
};

export class NotifyHandle {
    sync::Arc<NotifyState> m_state;

    explicit NotifyHandle(sync::Arc<NotifyState> state): m_state(rstd::move(state)) {}

    friend class Notify;

public:
    NotifyHandle(const NotifyHandle&)                        = delete;
    auto operator=(const NotifyHandle&) -> NotifyHandle&     = delete;
    NotifyHandle(NotifyHandle&&) noexcept                    = default;
    auto operator=(NotifyHandle&&) noexcept -> NotifyHandle& = default;
    ~NotifyHandle()                                          = default;

    auto clone() const -> NotifyHandle { return NotifyHandle { m_state.clone() }; }

    auto notify() const -> io::Result<empty> {
        if (! m_state) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        }
        return m_state->notify();
    }
};

export class NotifyFuture {
    sync::Arc<NotifyState>  m_state;
    sync::Arc<NotifyWaiter> m_waiter;
    bool                    m_completed { false };

    explicit NotifyFuture(sync::Arc<NotifyState> state)
        : m_state(rstd::move(state)), m_waiter(sync::Arc<NotifyWaiter>::make()) {}

    friend class Notify;

    void cancel() {
        if (m_state && ! m_completed) m_state->cancel(*m_waiter);
    }

public:
    using Output = io::Result<empty>;

    NotifyFuture(const NotifyFuture&)                    = delete;
    auto operator=(const NotifyFuture&) -> NotifyFuture& = delete;
    NotifyFuture(NotifyFuture&&) noexcept                = default;
    auto operator=(NotifyFuture&& other) noexcept -> NotifyFuture& {
        if (this != &other) {
            cancel();
            m_state     = rstd::move(other.m_state);
            m_waiter    = rstd::move(other.m_waiter);
            m_completed = other.m_completed;
        }
        return *this;
    }
    ~NotifyFuture() { cancel(); }

    auto poll(mut_ref<NotifyFuture> self, task::Context& cx) -> task::Poll<Output> {
        if (self->m_completed) rstd::panic { "async::NotifyFuture polled after completion" };
        if (! self->m_state->poll(*self->m_waiter, cx)) return task::Poll<Output>::Pending();
        self->m_completed = true;
        return task::Poll<Output>::Ready(Ok(empty {}));
    }
};

inline auto Notify::notifier() const -> NotifyHandle {
    return NotifyHandle { m_state.clone() };
}

inline auto Notify::notified() const -> NotifyFuture {
    return NotifyFuture { m_state.clone() };
}

} // namespace rstd::async

module;
#include <rstd/macro.hpp>
export module rstd:async.watch;
export import :async.forward;
import rstd.alloc;
import :sync;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::sync::Arc;
using rstd::sync::Mutex;
using rstd::sync::RwLock;
using rstd::sync::RwLockReadGuard;

namespace rstd::async::watch
{
export struct RecvError {
    friend constexpr auto operator==(RecvError, RecvError) noexcept -> bool = default;
};

struct Waiter {
    Waiter*             previous {};
    Waiter*             next {};
    Option<task::Waker> waker;
    bool                queued {};
};

template<typename T>
struct State {
    struct Status {
        u64     version {};
        usize   senders { 1 };
        Waiter* head {};
        Waiter* tail {};

        void unlink(Waiter& waiter) {
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
        void take_waiters(Vec<task::Waker>& wake) {
            while (head) {
                auto& waiter = *head;
                unlink(waiter);
                if (auto waker = waiter.waker.take()) wake.push(rstd::move(*waker));
            }
        }
    };

    RwLock<T>     value;
    Mutex<Status> status { Status {} };

    explicit State(T value): value(rstd::move(value)) {}

    void retain_sender() {
        auto state = status.lock().unwrap_unchecked();
        if (state->senders == usize::MAX) rstd::panic { "watch sender count exhausted" };
        ++state->senders;
    }
    void drop_sender() {
        Vec<task::Waker> wake;
        {
            auto state = status.lock().unwrap_unchecked();
            if (--state->senders == usize()) state->take_waiters(wake);
        }
        for (auto& waker : wake) rstd::move(waker).wake();
    }
    auto send_replace(T next) -> T {
        Option<T>        previous;
        Vec<task::Waker> wake;
        {
            auto data      = value.write().unwrap_unchecked();
            previous       = Some(rstd::exchange(*data, rstd::move(next)));
            auto state     = status.lock().unwrap_unchecked();
            state->version = state->version.wrapping_add(u64(1));
            state->take_waiters(wake);
        }
        for (auto& waker : wake) rstd::move(waker).wake();
        return rstd::move(*previous);
    }
    auto version() const -> u64 {
        auto state = status.lock().unwrap_unchecked();
        return state->version;
    }
    void cancel(Waiter& waiter) {
        Option<task::Waker> discarded;
        {
            auto state = status.lock().unwrap_unchecked();
            if (waiter.queued) state->unlink(waiter);
            discarded = waiter.waker.take();
        }
    }
    auto poll(Waiter& waiter, u64& seen, task::Context& cx)
        -> task::Poll<Result<empty, RecvError>> {
        using Output = task::Poll<Result<empty, RecvError>>;
        Option<task::Waker> discarded;
        auto                state = status.lock().unwrap_unchecked();
        if (seen != state->version || state->senders == usize()) {
            if (waiter.queued) state->unlink(waiter);
            discarded = waiter.waker.take();
            if (seen != state->version) {
                seen = state->version;
                return Output::Ready(Ok(empty {}));
            }
            return Output::Ready(Err(RecvError {}));
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
        return Output::Pending();
    }
};

export template<typename T>
class Sender;
export template<typename T>
class Receiver;

/// Keep the read borrow short and release it before awaiting or publishing.
export template<typename T>
class Ref {
    Arc<State<T>>              owner_;
    Option<RwLockReadGuard<T>> guard_;
    bool                       changed_;

    Ref(Arc<State<T>> owner, RwLockReadGuard<T> guard, bool changed)
        : owner_(rstd::move(owner)), guard_(Some(rstd::move(guard))), changed_(changed) {}
    friend class Sender<T>;
    friend class Receiver<T>;

public:
    USE_TRAIT(Ref)
    Ref(const Ref&)                    = delete;
    auto operator=(const Ref&) -> Ref& = delete;
    Ref(Ref&& other) noexcept
        : owner_(rstd::move(other.owner_)), guard_(other.guard_.take()), changed_(other.changed_) {}
    auto operator=(Ref&& other) noexcept -> Ref& {
        if (this != &other) {
            guard_   = None();
            owner_   = rstd::move(other.owner_);
            guard_   = other.guard_.take();
            changed_ = other.changed_;
        }
        return *this;
    }
    auto deref() const noexcept -> ref<T> { return guard_->deref(); }
    auto has_changed() const noexcept -> bool { return changed_; }
};

/// Exclusively borrows the receiver; do not access or move it until this future is destroyed.
export template<typename T>
class Changed {
    Arc<State<T>> state_;
    Arc<Waiter>   waiter_;
    u64*          seen_;
    bool          completed_ {};

    Changed(Arc<State<T>> state, u64& seen)
        : state_(rstd::move(state)), waiter_(Arc<Waiter>::make()), seen_(&seen) {}
    friend class Receiver<T>;

    void cancel() {
        if (state_ && ! completed_) state_->cancel(*waiter_);
    }

public:
    using Output                               = Result<empty, RecvError>;
    Changed(const Changed&)                    = delete;
    auto operator=(const Changed&) -> Changed& = delete;
    Changed(Changed&&) noexcept                = default;
    auto operator=(Changed&& other) noexcept -> Changed& {
        if (this != &other) {
            cancel();
            state_     = rstd::move(other.state_);
            waiter_    = rstd::move(other.waiter_);
            seen_      = other.seen_;
            completed_ = other.completed_;
        }
        return *this;
    }
    ~Changed() { cancel(); }

    auto poll(mut_ref<Changed> self, task::Context& cx) -> task::Poll<Output> {
        if (self->completed_) rstd::panic { "watch::Changed polled after completion" };
        auto result = self->state_->poll(*self->waiter_, *self->seen_, cx);
        if (result.is_ready()) self->completed_ = true;
        return result;
    }
};

export template<typename T>
class Receiver {
    Arc<State<T>> state_;
    u64           seen_;

    Receiver(Arc<State<T>> state, u64 seen): state_(rstd::move(state)), seen_(seen) {}
    friend class Sender<T>;

public:
    Receiver(const Receiver&)                        = delete;
    auto operator=(const Receiver&) -> Receiver&     = delete;
    Receiver(Receiver&&) noexcept                    = default;
    auto operator=(Receiver&&) noexcept -> Receiver& = default;

    auto clone() const -> Receiver { return Receiver(state_.clone(), seen_); }
    auto borrow() const -> Ref<T> {
        auto data    = state_->value.read().unwrap_unchecked();
        auto version = state_->version();
        return Ref<T>(state_.clone(), rstd::move(data), seen_ != version);
    }
    auto borrow_and_update() -> Ref<T> {
        auto data    = state_->value.read().unwrap_unchecked();
        auto version = state_->version();
        auto changed = seen_ != version;
        seen_        = version;
        return Ref<T>(state_.clone(), rstd::move(data), changed);
    }
    auto changed() & [[clang::lifetimebound]] -> Changed<T> {
        return Changed<T>(state_.clone(), seen_);
    }
};

export template<typename T>
class Sender {
    Arc<State<T>> state_;

    explicit Sender(Arc<State<T>> state): state_(rstd::move(state)) {}

public:
    explicit Sender(T value): state_(Arc<State<T>>::make(rstd::move(value))) {}
    Sender(const Sender&)                    = delete;
    auto operator=(const Sender&) -> Sender& = delete;
    Sender(Sender&&) noexcept                = default;
    auto operator=(Sender&& other) noexcept -> Sender& {
        if (this != &other) {
            if (state_) state_->drop_sender();
            state_ = rstd::move(other.state_);
        }
        return *this;
    }
    ~Sender() {
        if (state_) state_->drop_sender();
    }

    auto clone() const -> Sender {
        state_->retain_sender();
        return Sender(state_.clone());
    }
    auto subscribe() const -> Receiver<T> { return Receiver<T>(state_.clone(), state_->version()); }
    auto borrow() const -> Ref<T> {
        return Ref<T>(state_.clone(), state_->value.read().unwrap_unchecked(), false);
    }
    auto send_replace(T value) const -> T { return state_->send_replace(rstd::move(value)); }
};

export template<typename T>
auto channel(T value) -> tuple<Sender<T>, Receiver<T>> {
    auto sender   = Sender<T>(rstd::move(value));
    auto receiver = sender.subscribe();
    return tuple<Sender<T>, Receiver<T>> { rstd::move(sender), rstd::move(receiver) };
}
} // namespace rstd::async::watch

export namespace rstd
{
template<typename T>
struct Impl<ops::Deref, async::watch::Ref<T>> : ImplBase<async::watch::Ref<T>> {
    using Target = T;
    auto deref() const noexcept -> ref<T> { return this->self().deref(); }
};
template<>
struct Impl<fmt::Display, async::watch::RecvError> : ImplBase<async::watch::RecvError> {
    auto fmt(fmt::Formatter& f) const -> bool { return f.write_str("channel closed"_str); }
};
template<>
struct Impl<fmt::Debug, async::watch::RecvError> : Impl<fmt::Display, async::watch::RecvError> {};
template<>
struct Impl<error::Error, async::watch::RecvError>
    : DefaultInImpl<error::Error, async::watch::RecvError> {};
} // namespace rstd

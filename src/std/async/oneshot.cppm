export module rstd:async.oneshot;
export import :async.forward;
import rstd.alloc;
import :sync;

using namespace rstd;

namespace rstd::async::oneshot
{

export struct Canceled {
    friend constexpr auto operator==(Canceled, Canceled) noexcept -> bool = default;
};

template<typename T>
struct State {
    struct Fields {
        Option<T>           value;
        Option<task::Waker> receiver_waker;
        Option<task::Waker> sender_waker;
        bool                sender_closed { false };
        bool                receiver_closed { false };
        bool                consumed { false };
    };

    sync::Mutex<Fields> fields;

    State() : fields(Fields {}) {}

    auto send(T value) -> Result<empty, T> {
        auto receiver_waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->sender_closed || f->receiver_closed || f->value.is_some()) {
                return Err(rstd::move(value));
            }
            f->value         = Some(rstd::move(value));
            f->sender_closed = true;
            receiver_waker   = f->receiver_waker.take();
        }

        if (receiver_waker.is_some()) {
            rstd::move(*receiver_waker).wake();
        }
        return Ok(empty {});
    }

    void drop_sender() {
        auto receiver_waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->sender_closed) {
                return;
            }
            f->sender_closed = true;
            receiver_waker   = f->receiver_waker.take();
        }

        if (receiver_waker.is_some()) {
            rstd::move(*receiver_waker).wake();
        }
    }

    void drop_receiver() {
        auto sender_waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->receiver_closed) {
                return;
            }
            f->receiver_closed = true;
            f->receiver_waker  = None();
            sender_waker       = f->sender_waker.take();
        }

        if (sender_waker.is_some()) {
            rstd::move(*sender_waker).wake();
        }
    }

    auto poll_receiver(task::Context& cx) -> task::Poll<Result<T, Canceled>> {
        auto value        = Option<T> {};
        auto sender_waker = Option<task::Waker> {};
        auto canceled     = false;
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->consumed) {
                rstd::panic { "oneshot::Receiver polled after completion" };
            }

            if (f->value.is_some()) {
                f->consumed        = true;
                f->receiver_closed = true;
                value              = Some(rstd::move(f->value).unwrap_unchecked());
                sender_waker       = f->sender_waker.take();
            } else if (f->sender_closed || f->receiver_closed) {
                f->consumed        = true;
                f->receiver_closed = true;
                sender_waker       = f->sender_waker.take();
                canceled           = true;
            } else {
                f->receiver_waker = Some(cx.waker().clone());
                return task::Poll<Result<T, Canceled>>::Pending();
            }
        }

        if (sender_waker.is_some()) {
            rstd::move(*sender_waker).wake();
        }

        if (canceled) {
            return task::Poll<Result<T, Canceled>>::Ready(Err(Canceled {}));
        }
        return task::Poll<Result<T, Canceled>>::Ready(
            Ok(rstd::move(value).unwrap_unchecked()));
    }

    auto poll_canceled(task::Context& cx) -> task::Poll<void> {
        auto f = fields.lock().unwrap_unchecked();
        if (f->receiver_closed) {
            return task::Poll<void>::Ready();
        }
        f->sender_waker = Some(cx.waker().clone());
        return task::Poll<void>::Pending();
    }

    auto is_canceled() const -> bool {
        auto f = fields.lock().unwrap_unchecked();
        return f->receiver_closed;
    }
};

export template<typename T>
class Sender {
    sync::Arc<State<T>> state;
    bool               active { true };

public:
    explicit Sender(sync::Arc<State<T>> state) : state(rstd::move(state)) {}

    Sender(const Sender&)            = delete;
    Sender& operator=(const Sender&) = delete;

    Sender(Sender&& other) noexcept
        : state(rstd::move(other.state)), active(rstd::exchange(other.active, false)) {}

    auto operator=(Sender&& other) noexcept -> Sender& {
        if (this != &other) {
            close();
            state  = rstd::move(other.state);
            active = rstd::exchange(other.active, false);
        }
        return *this;
    }

    ~Sender() { close(); }

    auto send(T value) -> Result<empty, T> {
        if (! active || ! state) {
            return Err(rstd::move(value));
        }
        active = false;
        return state->send(rstd::move(value));
    }

    void close() {
        if (active && state) {
            active = false;
            state->drop_sender();
        }
    }

    auto is_canceled() const -> bool { return ! state || state->is_canceled(); }

    auto poll_canceled(mut_ref<Sender> self, task::Context& cx) -> task::Poll<void> {
        auto& value = *self;
        if (! value.active || ! value.state) {
            return task::Poll<void>::Ready();
        }
        return value.state->poll_canceled(cx);
    }
};

export template<typename T>
class Receiver {
    sync::Arc<State<T>> state;
    bool               active { true };

public:
    using Output = Result<T, Canceled>;

    explicit Receiver(sync::Arc<State<T>> state) : state(rstd::move(state)) {}

    Receiver(const Receiver&)            = delete;
    Receiver& operator=(const Receiver&) = delete;

    Receiver(Receiver&& other) noexcept
        : state(rstd::move(other.state)), active(rstd::exchange(other.active, false)) {}

    auto operator=(Receiver&& other) noexcept -> Receiver& {
        if (this != &other) {
            close();
            state  = rstd::move(other.state);
            active = rstd::exchange(other.active, false);
        }
        return *this;
    }

    ~Receiver() { close(); }

    void close() {
        if (active && state) {
            active = false;
            state->drop_receiver();
        }
    }

    auto poll(mut_ref<Receiver> self, task::Context& cx) -> task::Poll<Output> {
        auto& value = *self;
        if (! value.active || ! value.state) {
            return task::Poll<Output>::Ready(Err(Canceled {}));
        }

        auto out = value.state->poll_receiver(cx);
        if (out.is_ready()) {
            value.active = false;
        }
        return out;
    }
};

export template<typename T>
auto channel() -> tuple<Sender<T>, Receiver<T>> {
    auto state = sync::Arc<State<T>>::make();
    return tuple<Sender<T>, Receiver<T>> {
        Sender<T> { state.clone() },
        Receiver<T> { rstd::move(state) },
    };
}

} // namespace rstd::async::oneshot

export module rstd:async.completion;
export import :async.forward;
import :async.notify;
import :io;
import :sync;

using namespace rstd;

namespace rstd::async
{

export template<typename E>
class CompletionError {
public:
    enum class Kind {
        Canceled,
        Failed,
        Notify,
    };

private:
    Kind              m_kind;
    Option<E>         m_failed;
    Option<io::Error> m_notify;

    explicit CompletionError(Kind kind) : m_kind(kind), m_failed(None()), m_notify(None()) {}

public:
    static auto canceled() -> CompletionError {
        return CompletionError { Kind::Canceled };
    }

    static auto failed(E error) -> CompletionError {
        auto out     = CompletionError { Kind::Failed };
        out.m_failed = Some(rstd::move(error));
        return out;
    }

    static auto notify(io::Error error) -> CompletionError {
        auto out     = CompletionError { Kind::Notify };
        out.m_notify = Some(rstd::move(error));
        return out;
    }

    auto kind() const noexcept -> Kind { return m_kind; }
    auto is_canceled() const noexcept -> bool { return m_kind == Kind::Canceled; }
    auto is_failed() const noexcept -> bool { return m_kind == Kind::Failed; }
    auto is_notify() const noexcept -> bool { return m_kind == Kind::Notify; }

    auto unwrap_failed() && -> E {
        if (! is_failed() || m_failed.is_none()) {
            rstd::panic { "CompletionError::unwrap_failed called on non-failed error" };
        }
        return rstd::move(m_failed).unwrap_unchecked();
    }

    auto unwrap_notify() && -> io::Error {
        if (! is_notify() || m_notify.is_none()) {
            rstd::panic { "CompletionError::unwrap_notify called on non-notify error" };
        }
        return rstd::move(m_notify).unwrap_unchecked();
    }
};

export template<typename T, typename E = empty>
class Completion;

export template<typename T, typename E = empty>
class CompletionHandle;

template<typename T, typename E>
struct CompletionState {
    struct Fields {
        Option<T> value;
        Option<E> error;
        usize     handles { 1 };
        bool      canceled { false };
        bool      receiver_closed { false };
        bool      consumed { false };
    };

    sync::Mutex<Fields> fields;
    NotifyHandle        notifier;

    explicit CompletionState(NotifyHandle notifier)
        : fields(Fields {}), notifier(rstd::move(notifier)) {}

    CompletionState(const CompletionState&)            = delete;
    auto operator=(const CompletionState&) -> CompletionState& = delete;

    void wake() { (void)notifier.notify(); }

    void add_handle() {
        auto f = fields.lock().unwrap_unchecked();
        ++f->handles;
    }

    auto complete(T value) -> Result<empty, T> {
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->receiver_closed || f->value.is_some() || f->error.is_some() ||
                f->canceled) {
                return Err(rstd::move(value));
            }

            f->value = Some(rstd::move(value));
        }

        wake();
        return Ok(empty {});
    }

    auto fail(E error) -> Result<empty, E> {
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->receiver_closed || f->value.is_some() || f->error.is_some() ||
                f->canceled) {
                return Err(rstd::move(error));
            }

            f->error = Some(rstd::move(error));
        }

        wake();
        return Ok(empty {});
    }

    auto cancel() -> bool {
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->receiver_closed || f->value.is_some() || f->error.is_some() ||
                f->canceled) {
                return false;
            }

            f->canceled = true;
        }

        wake();
        return true;
    }

    void drop_handle() {
        auto should_wake = false;
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->handles > 0) {
                --f->handles;
            }
            if (f->handles == 0 && ! f->receiver_closed && f->value.is_none() &&
                f->error.is_none() && ! f->canceled) {
                f->canceled = true;
                should_wake = true;
            }
        }

        if (should_wake) {
            wake();
        }
    }

    void close_receiver() {
        auto f = fields.lock().unwrap_unchecked();
        f->receiver_closed = true;
    }

    auto is_closed() -> bool {
        auto f = fields.lock().unwrap_unchecked();
        return f->receiver_closed || f->value.is_some() || f->error.is_some() || f->canceled;
    }

    auto take_result() -> Option<Result<T, CompletionError<E>>> {
        auto f = fields.lock().unwrap_unchecked();
        if (f->consumed) {
            rstd::panic { "async::Completion polled after completion" };
        }

        if (f->value.is_some()) {
            f->consumed        = true;
            f->receiver_closed = true;
            return Some<Result<T, CompletionError<E>>>(
                Ok(rstd::move(f->value).unwrap_unchecked()));
        }

        if (f->error.is_some()) {
            f->consumed        = true;
            f->receiver_closed = true;
            return Some<Result<T, CompletionError<E>>>(Err(CompletionError<E>::failed(
                rstd::move(f->error).unwrap_unchecked())));
        }

        if (f->canceled || f->handles == 0) {
            f->consumed        = true;
            f->receiver_closed = true;
            return Some<Result<T, CompletionError<E>>>(
                Err(CompletionError<E>::canceled()));
        }

        return None();
    }
};

export template<typename T, typename E>
class Completion {
    sync::Arc<CompletionState<T, E>> m_state;
    Notify                          m_notify;
    Option<NotifyFuture>            m_wait;
    bool                            m_active { true };

    Completion(sync::Arc<CompletionState<T, E>> state, Notify notify)
        : m_state(rstd::move(state)),
          m_notify(rstd::move(notify)),
          m_wait(None()) {}

    friend class CompletionHandle<T, E>;

public:
    using Output = Result<T, CompletionError<E>>;

    Completion(const Completion&)            = delete;
    auto operator=(const Completion&) -> Completion& = delete;

    Completion(Completion&& other) noexcept
        : m_state(rstd::move(other.m_state)),
          m_notify(rstd::move(other.m_notify)),
          m_wait(other.m_wait.take()),
          m_active(rstd::exchange(other.m_active, false)) {}

    auto operator=(Completion&& other) noexcept -> Completion& {
        if (this != &other) {
            close();
            m_state  = rstd::move(other.m_state);
            m_notify = rstd::move(other.m_notify);
            m_wait   = other.m_wait.take();
            m_active = rstd::exchange(other.m_active, false);
        }
        return *this;
    }

    ~Completion() { close(); }

    static auto make() -> io::Result<tuple<Completion<T, E>, CompletionHandle<T, E>>> {
        auto notify = Notify::make();
        if (notify.is_err()) {
            return Err(rstd::move(notify).unwrap_err_unchecked());
        }

        auto notify_value = rstd::move(notify).unwrap_unchecked();
        auto notifier     = notify_value.notifier();
        auto state        = sync::Arc<CompletionState<T, E>>::make(rstd::move(notifier));

        return Ok(tuple<Completion<T, E>, CompletionHandle<T, E>> {
            Completion<T, E> { state.clone(), rstd::move(notify_value) },
            CompletionHandle<T, E> { rstd::move(state) },
        });
    }

    void close() {
        if (m_active && m_state) {
            m_active = false;
            m_state->close_receiver();
        }
    }

    auto poll(mut_ref<Completion> self, task::Context& cx)
        -> task::Poll<Output> {
        auto& completion = *self;
        if (! completion.m_active || ! completion.m_state) {
            return task::Poll<Output>::Ready(Err(CompletionError<E>::canceled()));
        }

        for (;;) {
            auto result = completion.m_state->take_result();
            if (result.is_some()) {
                completion.m_active = false;
                return task::Poll<Output>::Ready(
                    rstd::move(result).unwrap_unchecked());
            }

            if (completion.m_wait.is_none()) {
                completion.m_wait = Some(completion.m_notify.notified());
            }

            auto notified =
                completion.m_wait->poll(future::as_mut_ref(*completion.m_wait), cx);
            if (notified.is_pending()) {
                return task::Poll<Output>::Pending();
            }

            completion.m_wait = None();
            auto notify_result = rstd::move(notified).take();
            if (notify_result.is_err()) {
                completion.m_active = false;
                completion.m_state->close_receiver();
                return task::Poll<Output>::Ready(Err(CompletionError<E>::notify(
                    rstd::move(notify_result).unwrap_err_unchecked())));
            }
        }
    }
};

export template<typename T, typename E>
class CompletionHandle {
    sync::Arc<CompletionState<T, E>> m_state;
    bool                            m_active { true };

    explicit CompletionHandle(sync::Arc<CompletionState<T, E>> state)
        : m_state(rstd::move(state)) {}

    friend class Completion<T, E>;

public:
    CompletionHandle(const CompletionHandle&)            = delete;
    auto operator=(const CompletionHandle&) -> CompletionHandle& = delete;

    CompletionHandle(CompletionHandle&& other) noexcept
        : m_state(rstd::move(other.m_state)),
          m_active(rstd::exchange(other.m_active, false)) {}

    auto operator=(CompletionHandle&& other) noexcept -> CompletionHandle& {
        if (this != &other) {
            close();
            m_state  = rstd::move(other.m_state);
            m_active = rstd::exchange(other.m_active, false);
        }
        return *this;
    }

    ~CompletionHandle() { close(); }

    auto clone() -> CompletionHandle {
        if (! m_active || ! m_state) {
            rstd::panic { "CompletionHandle::clone called on closed handle" };
        }
        m_state->add_handle();
        return CompletionHandle { m_state.clone() };
    }

    void close() {
        if (m_active && m_state) {
            m_active = false;
            m_state->drop_handle();
        }
    }

    auto complete(T value) -> Result<empty, T> {
        if (! m_active || ! m_state) {
            return Err(rstd::move(value));
        }
        return m_state->complete(rstd::move(value));
    }

    auto fail(E error) -> Result<empty, E> {
        if (! m_active || ! m_state) {
            return Err(rstd::move(error));
        }
        return m_state->fail(rstd::move(error));
    }

    auto cancel() -> bool {
        if (! m_active || ! m_state) {
            return false;
        }
        return m_state->cancel();
    }

    auto is_closed() -> bool {
        return ! m_active || ! m_state || m_state->is_closed();
    }
};

} // namespace rstd::async

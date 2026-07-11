export module rstd:async.completion;
export import :async.forward;
import :async.awaitable;
import :async.terminal;
import :io;
import :sync;

using namespace rstd;

namespace rstd::async
{

export template<typename E>
class CompletionError {
public:
    enum class Kind
    {
        Canceled,
        Failed,
    };

private:
    Kind      m_kind;
    Option<E> m_failed;

    explicit CompletionError(Kind kind): m_kind(kind), m_failed(None()) {}

public:
    static auto canceled() -> CompletionError { return CompletionError { Kind::Canceled }; }

    static auto failed(E error) -> CompletionError {
        auto out     = CompletionError { Kind::Failed };
        out.m_failed = Some(rstd::move(error));
        return out;
    }

    auto kind() const noexcept -> Kind { return m_kind; }
    auto is_canceled() const noexcept -> bool { return m_kind == Kind::Canceled; }
    auto is_failed() const noexcept -> bool { return m_kind == Kind::Failed; }

    auto unwrap_failed() && -> E {
        if (! is_failed() || m_failed.is_none()) {
            rstd::panic { "CompletionError::unwrap_failed called on non-failed error" };
        }
        return rstd::move(m_failed).unwrap_unchecked();
    }
};

export template<typename T, typename E = empty>
class Completion;

template<typename T, typename E = empty>
class CompletionWaitOperation;

export template<typename T, typename E = empty>
class CompletionHandle;

template<typename T, typename E>
struct CompletionState {
    using Output = Result<T, CompletionError<E>>;

    struct Fields {
        TerminalCell<Output> terminal;
        usize                handles { 1 };
        Option<task::Waker>  waker;
    };

    sync::Mutex<Fields> fields;

    CompletionState(): fields(Fields {}) {}

    CompletionState(const CompletionState&)                    = delete;
    auto operator=(const CompletionState&) -> CompletionState& = delete;

    static void wake(Option<task::Waker> waker) {
        if (waker.is_some()) {
            rstd::move(*waker).wake();
        }
    }

    void add_handle() {
        auto f = fields.lock().unwrap_unchecked();
        ++f->handles;
    }

    auto complete(T value) -> Result<empty, T> {
        auto waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (! f->terminal.is_pending()) {
                return Err(rstd::move(value));
            }

            (void)f->terminal.publish(Output { Ok(rstd::move(value)) });
            waker = f->waker.take();
        }

        wake(rstd::move(waker));
        return Ok(empty {});
    }

    auto fail(E error) -> Result<empty, E> {
        auto waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (! f->terminal.is_pending()) {
                return Err(rstd::move(error));
            }

            (void)f->terminal.publish(
                Output { Err(CompletionError<E>::failed(rstd::move(error))) });
            waker = f->waker.take();
        }

        wake(rstd::move(waker));
        return Ok(empty {});
    }

    auto cancel() -> bool {
        auto waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (! f->terminal.is_pending()) {
                return false;
            }

            (void)f->terminal.publish(Output { Err(CompletionError<E>::canceled()) });
            waker = f->waker.take();
        }

        wake(rstd::move(waker));
        return true;
    }

    void drop_handle() {
        auto waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->handles > 0) {
                --f->handles;
            }
            if (f->handles == 0 && f->terminal.is_pending()) {
                (void)f->terminal.publish(Output { Err(CompletionError<E>::canceled()) });
                waker = f->waker.take();
            }
        }

        wake(rstd::move(waker));
    }

    void close_receiver() {
        auto f   = fields.lock().unwrap_unchecked();
        f->waker = None();
        f->terminal.close();
    }

    auto is_closed() -> bool {
        auto f = fields.lock().unwrap_unchecked();
        return f->terminal.is_terminal();
    }

    auto wait(const task::Waker& waker) -> Option<Output> {
        auto f = fields.lock().unwrap_unchecked();
        if (f->terminal.is_consumed()) {
            rstd::panic { "async::Completion awaited after completion" };
        }

        if (f->terminal.is_ready()) {
            f->waker = None();
            return Some(f->terminal.take());
        }
        if (f->terminal.is_closed()) {
            rstd::panic { "async::Completion awaited after close" };
        }

        f->waker = Some(waker.clone());
        return None();
    }
};

export template<typename T, typename E>
class Completion {
    sync::Arc<CompletionState<T, E>> m_state;
    bool                             m_active { true };

    explicit Completion(sync::Arc<CompletionState<T, E>> state): m_state(rstd::move(state)) {}

    friend class CompletionHandle<T, E>;
    friend class CompletionWaitOperation<T, E>;

public:
    using Output = Result<T, CompletionError<E>>;

    Completion(const Completion&)                    = delete;
    auto operator=(const Completion&) -> Completion& = delete;

    Completion(Completion&& other) noexcept
        : m_state(rstd::move(other.m_state)), m_active(rstd::exchange(other.m_active, false)) {}

    auto operator=(Completion&& other) noexcept -> Completion& {
        if (this != &other) {
            close();
            m_state  = rstd::move(other.m_state);
            m_active = rstd::exchange(other.m_active, false);
        }
        return *this;
    }

    ~Completion() { close(); }

    static auto make() -> io::Result<tuple<Completion<T, E>, CompletionHandle<T, E>>> {
        auto state = sync::Arc<CompletionState<T, E>>::make();

        return Ok(tuple<Completion<T, E>, CompletionHandle<T, E>> {
            Completion<T, E> { state.clone() },
            CompletionHandle<T, E> { rstd::move(state) },
        });
    }

    void close() {
        if (m_active && m_state) {
            m_active = false;
            m_state->close_receiver();
        }
    }
};

template<typename T, typename E>
class CompletionWaitOperation {
public:
    using Output = typename Completion<T, E>::Output;

private:
    Option<Completion<T, E>> m_owned;
    Completion<T, E>*        m_borrowed { nullptr };
    Option<Output>           m_result;

    auto completion() -> Completion<T, E>& {
        if (m_owned.is_some()) {
            return *m_owned;
        }
        return *m_borrowed;
    }

public:
    explicit CompletionWaitOperation(Completion<T, E>& completion)
        : m_borrowed(rstd::addressof(completion)) {}

    explicit CompletionWaitOperation(Completion<T, E>&& completion)
        : m_owned(Some(rstd::move(completion))) {}

    auto take_output() -> Output { return rstd::move(m_result).unwrap_unchecked(); }

    auto advance(AwaitContext& cx) -> AwaitTransition {
        if (cx.execution_domain() == ExecutionDomainKind::ExternalExecutor) {
            return AwaitTransition::return_to_owner();
        }
        auto& value = completion();
        if (! value.m_active || ! value.m_state) {
            m_result.insert(Err(CompletionError<E>::canceled()));
            return AwaitTransition::continue_();
        }

        auto out = value.m_state->wait(cx.waker());
        if (out.is_none()) {
            return AwaitTransition::suspend();
        }

        value.m_active = false;
        m_result.insert(rstd::move(out).unwrap_unchecked());
        return AwaitTransition::continue_();
    }
};

template<typename T, typename E>
struct AwaitableTraits<Completion<T, E>> {
    using Output = typename Completion<T, E>::Output;

    static auto make_suspension(Completion<T, E>& completion) {
        auto operation = CompletionWaitOperation<T, E> { completion };
        return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
    }

    static auto make_suspension(Completion<T, E>&& completion) {
        auto operation = CompletionWaitOperation<T, E> { rstd::move(completion) };
        return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
    }
};

export template<typename T, typename E>
class CompletionHandle {
    sync::Arc<CompletionState<T, E>> m_state;
    bool                             m_active { true };

    explicit CompletionHandle(sync::Arc<CompletionState<T, E>> state): m_state(rstd::move(state)) {}

    friend class Completion<T, E>;

public:
    CompletionHandle(const CompletionHandle&)                    = delete;
    auto operator=(const CompletionHandle&) -> CompletionHandle& = delete;

    CompletionHandle(CompletionHandle&& other) noexcept
        : m_state(rstd::move(other.m_state)), m_active(rstd::exchange(other.m_active, false)) {}

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

    auto is_closed() -> bool { return ! m_active || ! m_state || m_state->is_closed(); }
};

} // namespace rstd::async

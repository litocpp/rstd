export module rstd:async.completion_queue;
export import :async.forward;
import :async.awaitable;
import :io;
import :sync;
import rstd.alloc;

using namespace rstd;
using ::alloc::vec::Vec;

namespace rstd::async
{

export template<typename T>
class CompletionQueue;

export template<typename T>
class CompletionQueueHandle;

export template<typename T>
class CompletionQueueNext;

template<typename T>
class CompletionQueueNextWaitOperation;

template<typename T>
struct CompletionQueueState {
    struct Fields {
        Vec<T> items;
        usize  handles { 1 };
        bool   closed { false };
        bool   receiver_closed { false };
        Option<task::Waker> waker;
    };

    sync::Mutex<Fields> fields;

    CompletionQueueState() : fields(Fields {}) {}

    CompletionQueueState(const CompletionQueueState&)            = delete;
    auto operator=(const CompletionQueueState&) -> CompletionQueueState& = delete;

    static void wake_waiter(Option<task::Waker> waker) {
        if (waker.is_some()) {
            rstd::move(*waker).wake();
        }
    }

    auto wake() -> io::Result<empty> {
        auto waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            waker = f->waker.take();
        }
        wake_waiter(rstd::move(waker));
        return Ok(empty {});
    }

    void add_handle() {
        auto f = fields.lock().unwrap_unchecked();
        ++f->handles;
    }

    auto push(T item) -> Result<empty, T> {
        auto waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->closed || f->receiver_closed) {
                return Err(rstd::move(item));
            }

            f->items.push(rstd::move(item));
            waker = f->waker.take();
        }

        wake_waiter(rstd::move(waker));
        return Ok(empty {});
    }

    void close_queue() {
        auto waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (! f->closed) {
                f->closed = true;
                waker     = f->waker.take();
            }
        }

        wake_waiter(rstd::move(waker));
    }

    void drop_handle() {
        auto waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->handles > 0) {
                --f->handles;
            }
            if (f->handles == 0 && ! f->closed) {
                f->closed = true;
                waker     = f->waker.take();
            }
        }

        wake_waiter(rstd::move(waker));
    }

    void close_receiver() {
        auto waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            f->receiver_closed = true;
            f->closed          = true;
            f->items.clear();
            waker = f->waker.take();
        }
        wake_waiter(rstd::move(waker));
    }

    auto is_closed() -> bool {
        auto f = fields.lock().unwrap_unchecked();
        return f->receiver_closed || f->closed;
    }

    auto wait_next(const task::Waker& waker) -> Option<io::Result<Option<T>>> {
        auto f = fields.lock().unwrap_unchecked();
        if (! f->items.is_empty()) {
            return Some<io::Result<Option<T>>>(Ok(Some(f->items.remove(0))));
        }

        if (f->closed || f->receiver_closed || f->handles == 0) {
            return Some<io::Result<Option<T>>>(Ok(None<T>()));
        }

        f->waker = Some(waker.clone());
        return None();
    }
};

export template<typename T>
class CompletionQueueNext {
    sync::Arc<CompletionQueueState<T>> m_state;
    bool                               m_completed { false };

    explicit CompletionQueueNext(sync::Arc<CompletionQueueState<T>> state)
        : m_state(rstd::move(state)) {}

    friend class CompletionQueue<T>;
    friend class CompletionQueueNextWaitOperation<T>;

public:
    using Output = io::Result<Option<T>>;

    CompletionQueueNext(const CompletionQueueNext&)            = delete;
    auto operator=(const CompletionQueueNext&) -> CompletionQueueNext& = delete;
    CompletionQueueNext(CompletionQueueNext&&) noexcept                    = default;
    auto operator=(CompletionQueueNext&&) noexcept -> CompletionQueueNext& = default;
    ~CompletionQueueNext()                                                = default;
};

template<typename T>
class CompletionQueueNextWaitOperation {
public:
    using Output = typename CompletionQueueNext<T>::Output;

private:
    Option<CompletionQueueNext<T>> m_owned;
    CompletionQueueNext<T>*        m_borrowed { nullptr };
    Option<Output>                 m_result;

    auto next() -> CompletionQueueNext<T>& {
        if (m_owned.is_some()) {
            return *m_owned;
        }
        return *m_borrowed;
    }

public:
    explicit CompletionQueueNextWaitOperation(CompletionQueueNext<T>& next)
        : m_borrowed(rstd::addressof(next)) {}

    explicit CompletionQueueNextWaitOperation(CompletionQueueNext<T>&& next)
        : m_owned(Some(rstd::move(next))) {}

    auto take_output() -> Output { return rstd::move(m_result).unwrap_unchecked(); }

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        auto& value = next();
        if (value.m_completed) {
            rstd::panic { "async::CompletionQueueNext awaited after completion" };
        }

        auto out = value.m_state->wait_next(cx.waker());
        if (out.is_none()) {
            return AwaitOperationState::Pending;
        }

        value.m_completed = true;
        m_result.insert(rstd::move(out).unwrap_unchecked());
        return AwaitOperationState::Ready;
    }

    auto placement() const -> ResumePlacement {
        return ResumePlacement::runtime_worker();
    }
};

template<typename T>
struct AwaitableTraits<CompletionQueueNext<T>> {
    using Output = typename CompletionQueueNext<T>::Output;

    static auto make_suspension(CompletionQueueNext<T>& next) {
        auto operation = CompletionQueueNextWaitOperation<T> { next };
        return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
    }

    static auto make_suspension(CompletionQueueNext<T>&& next) {
        auto operation = CompletionQueueNextWaitOperation<T> { rstd::move(next) };
        return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
    }
};

export template<typename T>
class CompletionQueue {
    sync::Arc<CompletionQueueState<T>> m_state;
    bool                               m_active { true };

    explicit CompletionQueue(sync::Arc<CompletionQueueState<T>> state)
        : m_state(rstd::move(state)) {}

    friend class CompletionQueueHandle<T>;

public:
    CompletionQueue(const CompletionQueue&)            = delete;
    auto operator=(const CompletionQueue&) -> CompletionQueue& = delete;

    CompletionQueue(CompletionQueue&& other) noexcept
        : m_state(rstd::move(other.m_state)),
          m_active(rstd::exchange(other.m_active, false)) {}

    auto operator=(CompletionQueue&& other) noexcept -> CompletionQueue& {
        if (this != &other) {
            close();
            m_state  = rstd::move(other.m_state);
            m_active = rstd::exchange(other.m_active, false);
        }
        return *this;
    }

    ~CompletionQueue() { close(); }

    static auto make() -> io::Result<tuple<CompletionQueue<T>, CompletionQueueHandle<T>>> {
        auto state = sync::Arc<CompletionQueueState<T>>::make();

        return Ok(tuple<CompletionQueue<T>, CompletionQueueHandle<T>> {
            CompletionQueue<T> { state.clone() },
            CompletionQueueHandle<T> { rstd::move(state) },
        });
    }

    void close() {
        if (m_active && m_state) {
            m_active = false;
            m_state->close_receiver();
        }
    }

    auto next() -> CompletionQueueNext<T> {
        if (! m_state) {
            rstd::panic { "CompletionQueue::next called on empty queue" };
        }
        return CompletionQueueNext<T> { m_state.clone() };
    }
};

export template<typename T>
class CompletionQueueHandle {
    sync::Arc<CompletionQueueState<T>> m_state;
    bool                               m_active { true };

    explicit CompletionQueueHandle(sync::Arc<CompletionQueueState<T>> state)
        : m_state(rstd::move(state)) {}

    friend class CompletionQueue<T>;

public:
    CompletionQueueHandle(const CompletionQueueHandle&)            = delete;
    auto operator=(const CompletionQueueHandle&) -> CompletionQueueHandle& = delete;

    CompletionQueueHandle(CompletionQueueHandle&& other) noexcept
        : m_state(rstd::move(other.m_state)),
          m_active(rstd::exchange(other.m_active, false)) {}

    auto operator=(CompletionQueueHandle&& other) noexcept -> CompletionQueueHandle& {
        if (this != &other) {
            drop();
            m_state  = rstd::move(other.m_state);
            m_active = rstd::exchange(other.m_active, false);
        }
        return *this;
    }

    ~CompletionQueueHandle() { drop(); }

    auto clone() -> CompletionQueueHandle {
        if (! m_active || ! m_state) {
            rstd::panic { "CompletionQueueHandle::clone called on closed handle" };
        }
        m_state->add_handle();
        return CompletionQueueHandle { m_state.clone() };
    }

    void drop() {
        if (m_active && m_state) {
            m_active = false;
            m_state->drop_handle();
        }
    }

    void close() {
        if (m_active && m_state) {
            m_state->close_queue();
            drop();
        }
    }

    auto push(T item) -> Result<empty, T> {
        if (! m_active || ! m_state) {
            return Err(rstd::move(item));
        }
        return m_state->push(rstd::move(item));
    }

    auto wake() -> io::Result<empty> {
        if (! m_active || ! m_state) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        }
        return m_state->wake();
    }

    auto is_closed() -> bool {
        return ! m_active || ! m_state || m_state->is_closed();
    }
};

} // namespace rstd::async

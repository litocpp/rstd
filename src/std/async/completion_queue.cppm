export module rstd:async.completion_queue;
export import :async.forward;
import :async.notify;
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
struct CompletionQueueState {
    struct Fields {
        Vec<T> items;
        usize  handles { 1 };
        bool   closed { false };
        bool   receiver_closed { false };
    };

    sync::Mutex<Fields> fields;
    NotifyHandle        notifier;

    explicit CompletionQueueState(NotifyHandle notifier)
        : fields(Fields {}), notifier(rstd::move(notifier)) {}

    CompletionQueueState(const CompletionQueueState&)            = delete;
    auto operator=(const CompletionQueueState&) -> CompletionQueueState& = delete;

    auto wake() -> io::Result<empty> { return notifier.notify(); }

    void add_handle() {
        auto f = fields.lock().unwrap_unchecked();
        ++f->handles;
    }

    auto push(T item) -> Result<empty, T> {
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->closed || f->receiver_closed) {
                return Err(rstd::move(item));
            }

            f->items.push(rstd::move(item));
        }

        (void)wake();
        return Ok(empty {});
    }

    void close_queue() {
        auto should_wake = false;
        {
            auto f = fields.lock().unwrap_unchecked();
            if (! f->closed) {
                f->closed  = true;
                should_wake = true;
            }
        }

        if (should_wake) {
            (void)wake();
        }
    }

    void drop_handle() {
        auto should_wake = false;
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->handles > 0) {
                --f->handles;
            }
            if (f->handles == 0 && ! f->closed) {
                f->closed  = true;
                should_wake = true;
            }
        }

        if (should_wake) {
            (void)wake();
        }
    }

    void close_receiver() {
        auto f = fields.lock().unwrap_unchecked();
        f->receiver_closed = true;
        f->closed          = true;
        f->items.clear();
    }

    auto is_closed() -> bool {
        auto f = fields.lock().unwrap_unchecked();
        return f->receiver_closed || f->closed;
    }

    auto take_next() -> Option<Option<T>> {
        auto f = fields.lock().unwrap_unchecked();
        if (! f->items.is_empty()) {
            return Some<Option<T>>(Some(f->items.remove(0)));
        }

        if (f->closed || f->receiver_closed || f->handles == 0) {
            return Some<Option<T>>(None<T>());
        }

        return None();
    }
};

export template<typename T>
class CompletionQueueNext {
    sync::Arc<CompletionQueueState<T>> m_state;
    Notify                             m_notify;
    Option<NotifyFuture>               m_wait;
    bool                               m_completed { false };

    CompletionQueueNext(sync::Arc<CompletionQueueState<T>> state, Notify notify)
        : m_state(rstd::move(state)),
          m_notify(rstd::move(notify)),
          m_wait(None()) {}

    friend class CompletionQueue<T>;

public:
    using Output = io::Result<Option<T>>;

    CompletionQueueNext(const CompletionQueueNext&)            = delete;
    auto operator=(const CompletionQueueNext&) -> CompletionQueueNext& = delete;
    CompletionQueueNext(CompletionQueueNext&&) noexcept                    = default;
    auto operator=(CompletionQueueNext&&) noexcept -> CompletionQueueNext& = default;
    ~CompletionQueueNext()                                                = default;

    auto poll(pin::Pin<mut_ref<CompletionQueueNext>> self, task::Context& cx)
        -> task::Poll<Output> {
        auto& next = *self.get_unchecked_mut();
        if (next.m_completed) {
            rstd::panic { "async::CompletionQueueNext polled after completion" };
        }

        for (;;) {
            auto item = next.m_state->take_next();
            if (item.is_some()) {
                next.m_completed = true;
                return task::Poll<Output>::Ready(
                    Ok(rstd::move(item).unwrap_unchecked()));
            }

            if (next.m_wait.is_none()) {
                next.m_wait = Some(next.m_notify.notified());
            }

            auto notified = next.m_wait->poll(future::pin_mut(*next.m_wait), cx);
            if (notified.is_pending()) {
                return task::Poll<Output>::Pending();
            }

            next.m_wait = None();
            auto notify_result = rstd::move(notified).take();
            if (notify_result.is_err()) {
                next.m_completed = true;
                return task::Poll<Output>::Ready(
                    Err(rstd::move(notify_result).unwrap_err_unchecked()));
            }
        }
    }
};

export template<typename T>
class CompletionQueue {
    sync::Arc<CompletionQueueState<T>> m_state;
    Notify                             m_notify;
    bool                               m_active { true };

    CompletionQueue(sync::Arc<CompletionQueueState<T>> state, Notify notify)
        : m_state(rstd::move(state)),
          m_notify(rstd::move(notify)) {}

    friend class CompletionQueueHandle<T>;

public:
    CompletionQueue(const CompletionQueue&)            = delete;
    auto operator=(const CompletionQueue&) -> CompletionQueue& = delete;

    CompletionQueue(CompletionQueue&& other) noexcept
        : m_state(rstd::move(other.m_state)),
          m_notify(rstd::move(other.m_notify)),
          m_active(rstd::exchange(other.m_active, false)) {}

    auto operator=(CompletionQueue&& other) noexcept -> CompletionQueue& {
        if (this != &other) {
            close();
            m_state  = rstd::move(other.m_state);
            m_notify = rstd::move(other.m_notify);
            m_active = rstd::exchange(other.m_active, false);
        }
        return *this;
    }

    ~CompletionQueue() { close(); }

    static auto make() -> io::Result<tuple<CompletionQueue<T>, CompletionQueueHandle<T>>> {
        auto notify = Notify::make();
        if (notify.is_err()) {
            return Err(rstd::move(notify).unwrap_err_unchecked());
        }

        auto notify_value = rstd::move(notify).unwrap_unchecked();
        auto notifier     = notify_value.notifier();
        auto state        = sync::Arc<CompletionQueueState<T>>::make(rstd::move(notifier));

        return Ok(tuple<CompletionQueue<T>, CompletionQueueHandle<T>> {
            CompletionQueue<T> { state.clone(), rstd::move(notify_value) },
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
        return CompletionQueueNext<T> { m_state.clone(), m_notify.clone() };
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

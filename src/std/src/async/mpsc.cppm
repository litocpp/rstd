export module rstd:async.mpsc;
export import :async.coro_future;
export import :sync.mpmc.error;
import :async.awaitable;
import :async.mutex;
import :async.notify;
import :async.semaphore;
import :sync;
import rstd.alloc;

using namespace rstd::prelude;
using ::alloc::collections::VecDeque;
using rstd::sync::Arc;

namespace rstd::async::mpsc
{
template<class T>
struct State {
    struct Entry {
        T                    value;
        OwnedSemaphorePermit permit;
    };
    struct Fields {
        VecDeque<Entry> queue;
        usize           senders { 1 };
        bool            closed {};
        bool            receiver { true };
    };
    sync::Mutex<Fields>       fields { Fields {} };
    Semaphore                 capacity;
    Notify                    readable { Notify::make().unwrap() };
    rstd::async::Mutex<empty> receive_gate { empty {} };

    explicit State(usize count): capacity(count) {}

    void add_sender() {
        auto data = fields.lock().unwrap_unchecked();
        ++data->senders;
    }
    void remove_sender() {
        bool last;
        {
            auto data = fields.lock().unwrap_unchecked();
            last      = --data->senders == usize();
        }
        if (last) (void)readable.notifier().notify();
    }
    void close(bool drop) {
        Option<VecDeque<Entry>> discarded;
        {
            auto data    = fields.lock().unwrap_unchecked();
            data->closed = true;
            if (drop) {
                data->receiver = false;
                discarded      = Some(rstd::move(data->queue));
                data->queue    = VecDeque<Entry> {};
            }
        }
        capacity.close();
        (void)readable.notifier().notify();
    }
};

export template<class T>
class Receiver;
export template<class T>
class Sender;
export template<class T>
auto channel(usize capacity) -> tuple<Sender<T>, Receiver<T>>;

export template<class T>
class Sender {
    Arc<State<T>> state_;
    explicit Sender(Arc<State<T>> state): state_(rstd::move(state)) {}
    friend auto channel<T>(usize) -> tuple<Sender, Receiver<T>>;

    void release() {
        auto state = rstd::move(state_);
        if (state) state->remove_sender();
    }
    static auto send_owned(Sender sender, T value)
        -> coro<Result<empty, sync::mpmc::SendError<T>>> {
        auto permit = co_await sender.state_->capacity.acquire_owned();
        if (permit.is_err()) co_return Err(sync::mpmc::SendError<T> { rstd::move(value) });
        {
            auto data = sender.state_->fields.lock().unwrap_unchecked();
            if (data->closed) co_return Err(sync::mpmc::SendError<T> { rstd::move(value) });
            data->queue.push_back(
                typename State<T>::Entry { rstd::move(value), rstd::move(*permit) });
        }
        (void)sender.state_->readable.notifier().notify();
        co_return Ok(empty {});
    }

public:
    Sender(const Sender&)                    = delete;
    auto operator=(const Sender&) -> Sender& = delete;
    Sender(Sender&&) noexcept                = default;
    auto operator=(Sender&& other) noexcept -> Sender& {
        if (this != &other) {
            release();
            state_ = rstd::move(other.state_);
        }
        return *this;
    }
    ~Sender() { release(); }
    auto clone() const -> Sender {
        state_->add_sender();
        return Sender(state_.clone());
    }
    auto send(T value) const -> coro<Result<empty, sync::mpmc::SendError<T>>> {
        return send_owned(clone(), rstd::move(value));
    }
    auto is_closed() const -> bool { return state_->capacity.is_closed(); }
};

export template<class T>
class Receiver {
    Arc<State<T>> state_;
    explicit Receiver(Arc<State<T>> state): state_(rstd::move(state)) {}
    friend auto channel<T>(usize) -> tuple<Sender<T>, Receiver>;

    void release() {
        auto state = rstd::move(state_);
        if (state) state->close(true);
    }
    static auto recv_owned(Arc<State<T>> state) -> coro<Option<T>> {
        auto receiver = co_await state->receive_gate.lock();
        for (;;) {
            Option<typename State<T>::Entry> entry;
            {
                auto data = state->fields.lock().unwrap_unchecked();
                if (! data->receiver) co_return None();
                entry = data->queue.pop_front();
                if (entry.is_none() && (data->closed || data->senders == usize())) co_return None();
            }
            if (entry.is_some()) co_return Some(rstd::move(entry->value));
            (void)co_await state->readable.notified();
        }
    }

public:
    Receiver(const Receiver&)                    = delete;
    auto operator=(const Receiver&) -> Receiver& = delete;
    Receiver(Receiver&&) noexcept                = default;
    auto operator=(Receiver&& other) noexcept -> Receiver& {
        if (this != &other) {
            release();
            state_ = rstd::move(other.state_);
        }
        return *this;
    }
    ~Receiver() { release(); }
    auto recv() const -> coro<Option<T>> { return recv_owned(state_.clone()); }
    void close() const { state_->close(false); }
};

export template<class T>
auto channel(usize capacity) -> tuple<Sender<T>, Receiver<T>> {
    if (capacity == usize()) rstd::panic { "async::mpsc requires nonzero capacity" };
    auto state = Arc<State<T>>::make(capacity);
    return { Sender<T>(state.clone()), Receiver<T>(rstd::move(state)) };
}
} // namespace rstd::async::mpsc

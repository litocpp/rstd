module rstd;
import :net.completion_stream;
import :async.runtime_core;
import :async.spawn;
import :async.notify;
import :async.oneshot;
import :sync;

using namespace rstd::prelude;
namespace rstd::net
{
using CloseSender = async::oneshot::Sender<io::Result<empty>>;
struct CompletionTcpState {
    struct Fields {
        Option<TcpStream>                        stream;
        Vec<CloseSender, alloc::SharedAllocator> waiters;
        Option<io::Error>                        error;
        bool started {}, stopping {}, closed {}, reading {}, writing {}, write_closed {};
        Fields(TcpStream stream, alloc::SharedAllocator allocator)
            : stream(Some(rstd::move(stream))),
              waiters(decltype(waiters)::make_in(rstd::move(allocator))) {}
    };
    sync::Mutex<Fields> fields;
    async::Notify       notice;
    CompletionTcpState(TcpStream stream, async::Notify notice, alloc::SharedAllocator allocator)
        : fields(Fields(rstd::move(stream), rstd::move(allocator))), notice(rstd::move(notice)) {}
    void stop() const {
        {
            auto state      = fields.lock().unwrap();
            state->stopping = true;
            if (state->stream) (void)state->stream->shutdown_both();
        }
        (void)notice.notifier().notify();
    }
};
auto tcp_completion_error(io::ErrorKind::Entity kind = io::ErrorKind::NotConnected) -> io::Error {
    return io::Error::from_kind(io::ErrorKind { kind });
}
auto close_completion_tcp(sync::Arc<CompletionTcpState> state) -> async::coro<empty> {
    Option<TcpStream> stream;
    while (true) {
        auto wake = state->notice.notified();
        {
            auto fields = state->fields.lock().unwrap();
            if (fields->stopping) {
                stream = fields->stream.take();
                break;
            }
        }
        (void)co_await rstd::move(wake);
    }
    Option<io::Error> error;
    if (stream) {
        auto result = co_await rstd::move(*stream).close();
        if (result.is_err()) error = Some(rstd::move(result).unwrap_err());
    }
    auto waiters = [&] {
        auto fields    = state->fields.lock().unwrap();
        fields->closed = true;
        if (error) fields->error = Some(io::Error(*error));
        return rstd::move(fields->waiters);
    }();
    for (auto& waiter : waiters) {
        if (error)
            (void)rstd::move(waiter).send(Err(io::Error(*error)));
        else
            (void)rstd::move(waiter).send(Ok(empty {}));
    }
    co_return empty {};
}
struct CompletionTcp {
    sync::Arc<CompletionTcpState> state;
    auto                          start() const -> io::Result<empty> {
        auto fields = state->fields.lock().unwrap();
        if (fields->started) return Ok(empty {});
        if (! ::CURRENT_RUNTIME || ! ::CURRENT_RUNTIME->io_enabled())
            return Err(tcp_completion_error(io::ErrorKind::Unsupported));
        fields->started = true;
        (void)async::spawn(close_completion_tcp(state.clone()));
        return Ok(empty {});
    }
    void request_stop() const { state->stop(); }
};
struct CompletionTcpOperation {
    sync::Arc<CompletionTcpState> state;
    bool                          write;
    bool                          active { true };
    CompletionTcpOperation(sync::Arc<CompletionTcpState> state, bool write)
        : state(rstd::move(state)), write(write) {}
    CompletionTcpOperation(const CompletionTcpOperation&) = delete;
    CompletionTcpOperation(CompletionTcpOperation&& other) noexcept
        : state(rstd::move(other.state)),
          write(other.write),
          active(rstd::exchange(other.active, false)) {}
    ~CompletionTcpOperation() {
        if (! active) return;
        {
            auto fields                                 = state->fields.lock().unwrap();
            (write ? fields->writing : fields->reading) = false;
        }
    }
};
auto receive_tcp_completion(CompletionTcpOperation          active,
                            async::coro<net::StreamReceive> receiving)
    -> async::coro<async::io::StreamReceive> {
    auto result = co_await rstd::move(receiving);
    co_return result;
}
auto send_tcp_completion(CompletionTcpOperation active, async::coro<io::Result<usize>> sending)
    -> async::coro<io::Result<usize>> {
    auto result = co_await rstd::move(sending);
    co_return result;
}
auto receive_tcp_rejected(bytes::BytesMut buffer, io::Error error)
    -> async::coro<async::io::StreamReceive> {
    co_return async::io::StreamReceive { Err(rstd::move(error)), rstd::move(buffer) };
}
template<class T>
auto completion_tcp_ready(io::Result<T> result) -> async::coro<io::Result<T>> {
    co_return rstd::move(result);
}
auto wait_tcp_closed(sync::Arc<CompletionTcpState>               state,
                     async::oneshot::Receiver<io::Result<empty>> receiver)
    -> async::coro<io::Result<empty>> {
    auto reply = co_await rstd::move(receiver);
    if (reply.is_err()) co_return Err(tcp_completion_error(io::ErrorKind::Interrupted));
    co_return rstd::move(reply).unwrap();
}
} // namespace rstd::net

template<>
struct rstd::Impl<rstd::async::io::CompletionStream, rstd::net::CompletionTcp>
    : rstd::ImplBase<rstd::net::CompletionTcp> {
    auto recv_into(rstd::bytes::BytesMut buffer) const
        -> rstd::async::coro<rstd::async::io::StreamReceive> {
        auto start = self().start();
        if (start.is_err())
            return rstd::net::receive_tcp_rejected(rstd::move(buffer),
                                                   rstd::move(start).unwrap_err());
        auto fields = self().state->fields.lock().unwrap();
        if (fields->stopping || fields->closed || ! fields->stream)
            return rstd::net::receive_tcp_rejected(rstd::move(buffer),
                                                   rstd::net::tcp_completion_error());
        if (fields->reading)
            return rstd::net::receive_tcp_rejected(
                rstd::move(buffer),
                rstd::net::tcp_completion_error(rstd::io::ErrorKind::ResourceBusy));
        fields->reading = true;
        return rstd::net::receive_tcp_completion(
            rstd::net::CompletionTcpOperation(self().state.clone(), false),
            fields->stream->recv_into(rstd::move(buffer)));
    }
    auto send(rstd::bytes::Bytes data) const -> rstd::async::coro<rstd::io::Result<rstd::usize>> {
        auto start = self().start();
        if (start.is_err())
            return rstd::net::completion_tcp_ready<rstd::usize>(
                rstd::Err(rstd::move(start).unwrap_err()));
        auto fields = self().state->fields.lock().unwrap();
        if (fields->stopping || fields->closed || fields->write_closed || ! fields->stream)
            return rstd::net::completion_tcp_ready<rstd::usize>(
                rstd::Err(rstd::net::tcp_completion_error()));
        if (fields->writing)
            return rstd::net::completion_tcp_ready<rstd::usize>(
                rstd::Err(rstd::net::tcp_completion_error(rstd::io::ErrorKind::ResourceBusy)));
        fields->writing = true;
        return rstd::net::send_tcp_completion(
            rstd::net::CompletionTcpOperation(self().state.clone(), true),
            fields->stream->send(rstd::move(data)));
    }
    auto flush() const -> rstd::async::coro<rstd::io::Result<rstd::empty>> {
        auto fields = self().state->fields.lock().unwrap();
        if (fields->stopping || fields->closed)
            return rstd::net::completion_tcp_ready<rstd::empty>(
                rstd::Err(rstd::net::tcp_completion_error()));
        if (fields->writing)
            return rstd::net::completion_tcp_ready<rstd::empty>(
                rstd::Err(rstd::net::tcp_completion_error(rstd::io::ErrorKind::ResourceBusy)));
        return rstd::net::completion_tcp_ready<rstd::empty>(rstd::Ok(rstd::empty {}));
    }
    auto shutdown_write() const -> rstd::async::coro<rstd::io::Result<rstd::empty>> {
        auto fields = self().state->fields.lock().unwrap();
        if (fields->stopping || fields->closed || ! fields->stream)
            return rstd::net::completion_tcp_ready<rstd::empty>(
                rstd::Err(rstd::net::tcp_completion_error()));
        if (fields->writing)
            return rstd::net::completion_tcp_ready<rstd::empty>(
                rstd::Err(rstd::net::tcp_completion_error(rstd::io::ErrorKind::ResourceBusy)));
        if (fields->write_closed)
            return rstd::net::completion_tcp_ready<rstd::empty>(rstd::Ok(rstd::empty {}));
        auto result = fields->stream->shutdown();
        if (result.is_ok()) fields->write_closed = true;
        return rstd::net::completion_tcp_ready<rstd::empty>(rstd::move(result));
    }
    void request_stop() const { self().request_stop(); }
    auto close() const -> rstd::async::coro<rstd::io::Result<rstd::empty>> {
        auto start = self().start();
        if (start.is_err())
            return rstd::net::completion_tcp_ready<rstd::empty>(
                rstd::Err(rstd::move(start).unwrap_err()));
        auto pair = rstd::async::oneshot::channel<rstd::io::Result<rstd::empty>>();
        {
            auto fields = self().state->fields.lock().unwrap();
            if (fields->closed)
                return rstd::net::completion_tcp_ready<rstd::empty>(
                    fields->error
                        ? rstd::io::Result<rstd::empty>(rstd::Err(rstd::io::Error(*fields->error)))
                        : rstd::io::Result<rstd::empty>(rstd::Ok(rstd::empty {})));
            fields->waiters.push(rstd::move(pair.template get<0>()));
        }
        self().request_stop();
        return rstd::net::wait_tcp_closed(self().state.clone(), rstd::move(pair.template get<1>()));
    }
};

namespace rstd::net
{
auto as_stream(TcpStream stream, alloc::SharedAllocator allocator)
    -> io::Result<async::io::Stream> {
    auto notice = async::Notify::make();
    if (notice.is_err()) return Err(rstd::move(notice).unwrap_err());
    auto state = sync::Arc<CompletionTcpState>::make_in(
        allocator.clone(), rstd::move(stream), rstd::move(notice).unwrap(), allocator.clone());
    auto backend = sync::Arc<dyn<async::io::CompletionStream>>::make_in(
        rstd::move(allocator), CompletionTcp { rstd::move(state) });
    return Ok(async::io::Stream(rstd::move(backend)));
}
} // namespace rstd::net

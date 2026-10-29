module rstd;
import :async.debug.server;
import :async.runtime;
import :async.spawn;
import :async.select;
import :async.time;
import :sync;
import :thread;

using namespace rstd;
using namespace rstd::literals;
using ::alloc::vec::Vec;
using ::alloc::string::String;

namespace rstd::async::debug
{
void ServerState::stop() {
    auto wake = Vec<task::Waker>::make();
    {
        auto f     = fields.lock().unwrap_unchecked();
        f->stopped = true;
        for (auto& w : f->waiters)
            if (w) wake.push(w.take().unwrap());
    }
    for (auto& w : wake) rstd::move(w).wake();
}
class Stopped {
    sync::Arc<ServerState> state_;
    size_t                 slot_;

public:
    using Output = void;
    Stopped(sync::Arc<ServerState> state, size_t slot): state_(rstd::move(state)), slot_(slot) {}
    Stopped(Stopped&&) noexcept = default;
    ~Stopped() {
        if (state_) state_->fields.lock().unwrap()->waiters[usize(slot_)] = None<task::Waker>();
    }
    auto poll(mut_ref<Stopped> self, task::Context& cx) -> task::Poll<void> {
        auto f = self->state_->fields.lock().unwrap();
        if (f->stopped) return task::Poll<void>::Ready();
        f->waiters[usize(self->slot_)] = Some(cx.waker().clone());
        return task::Poll<void>::Pending();
    }
};
auto client(sync::Arc<ServerState> state, net::TcpStream socket, size_t slot)
    -> coro<rstd::io::Result<empty>> {
    protocol::Options options { state->options.max_message_bytes,
                                state->options.max_snapshot_bytes,
                                state->options.page_records,
                                state->options.snapshot_lifetime };
    protocol::Session session(state->debug, options);
    auto              pending = Vec<u8>::make();
    bool              finish  = false;
    while (! finish) {
        auto read     = async::timeout(async::into_future(socket.recv(usize(4096))),
                                       state->options.idle_timeout);
        auto selected = co_await async::select(Stopped(state.clone(), slot), rstd::move(read));
        if (selected.is_left()) break;
        auto result = rstd::move(selected).unwrap_right();
        if (result.is_err() || result->is_err() || (**result).is_empty()) break;
        for (auto c : (**result).as_slice()) {
            if (c == u8('\n')) {
                auto request  = protocol::parse_request(pending.as_slice());
                auto response = request.is_ok()
                                    ? session.respond(*request)
                                    : protocol::error_response(rstd::move(request).unwrap_err(),
                                                               options.max_message_bytes);
                pending.clear();
                if (response.is_empty()) {
                    finish = true;
                    break;
                }
                response.push_str("\n"_str);
                if (response.len().to_primitive() > state->options.max_message_bytes) {
                    finish = true;
                    break;
                }
                auto send =
                    async::timeout(async::into_future(socket.send_all(
                                       bytes::Bytes::copy_from_slice(response->as_bytes()))),
                                   state->options.idle_timeout);
                auto sent = co_await async::select(Stopped(state.clone(), slot), rstd::move(send));
                if (sent.is_left()) {
                    finish = true;
                    break;
                }
                auto out = rstd::move(sent).unwrap_right();
                if (out.is_err() || out->is_err()) {
                    finish = true;
                    break;
                }
            } else {
                if (pending.len().to_primitive() + 1 >= state->options.max_message_bytes) {
                    finish = true;
                    break;
                }
                pending.push(u8(c));
            }
        }
    }
    co_return co_await rstd::move(socket).close();
}
void startup(const sync::Arc<ServerState>& state, rstd::io::Result<net::SocketAddr> result) {
    {
        auto f     = state->fields.lock().unwrap();
        f->startup = Some(rstd::move(result));
    }
    state->ready.notify_all();
}
void client_error(Option<rstd::io::Error>&                   error,
                  Result<rstd::io::Result<empty>, JoinError> result) {
    if (error) return;
    if (result.is_err())
        error =
            Some(rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Other }));
    else if (result->is_err())
        error = Some(rstd::move(*result).unwrap_err());
}
auto serve(sync::Arc<ServerState> state) -> coro<rstd::io::Result<empty>> {
    auto bound = net::TcpListener::bind(state->options.address);
    if (bound.is_err()) {
        startup(state, Err(rstd::move(bound).unwrap_err()));
        co_return Ok(empty {});
    }
    auto listener = rstd::move(bound).unwrap();
    auto address  = listener.local_addr();
    if (address.is_err()) {
        startup(state, Err(rstd::move(address).unwrap_err()));
        (void)co_await rstd::move(listener).close();
        co_return Ok(empty {});
    }
    startup(state, Ok(*address));
    auto clients = Vec<Option<async::JoinHandle<rstd::io::Result<empty>>>>::make();
    for (size_t i = 0; i < state->options.max_connections; ++i)
        clients.push(None<async::JoinHandle<rstd::io::Result<empty>>>());
    Option<rstd::io::Error> error;
    while (true) {
        auto accepted = co_await async::select(Stopped(state.clone(), 0),
                                               async::into_future(listener.accept()));
        if (accepted.is_left()) break;
        auto result = rstd::move(accepted).unwrap_right();
        if (result.is_err()) {
            error = Some(rstd::move(result).unwrap_err());
            break;
        }
        auto   socket = rstd::move(result->template get<0>());
        size_t slot   = state->options.max_connections;
        for (usize i {}; i < clients.len(); ++i) {
            if (clients[i] && clients[i]->is_finished()) {
                client_error(error, co_await clients[i].take().unwrap());
            }
            if (! clients[i] && slot == state->options.max_connections) slot = i.to_primitive();
        }
        if (slot == state->options.max_connections) {
            auto closed = co_await rstd::move(socket).close();
            if (closed.is_err() && ! error) error = Some(rstd::move(closed).unwrap_err());
            continue;
        }
        clients[usize(slot)] =
            Some(async::spawn(client(state.clone(), rstd::move(socket), slot + 1)));
    }
    state->stop();
    auto closed = co_await rstd::move(listener).close();
    if (closed.is_err() && ! error) error = Some(rstd::move(closed).unwrap_err());
    for (auto& c : clients)
        if (c) client_error(error, co_await c.take().unwrap());
    if (error) co_return Err(error.take().unwrap());
    co_return Ok(empty {});
}
Server::Server(sync::Arc<ServerState>                      state,
               thread::JoinHandle<rstd::io::Result<empty>> thread,
               net::SocketAddr                             address)
    : state_(rstd::move(state)), thread_(Some(rstd::move(thread))), address_(address) {
}
Server::Server(Server&& other) noexcept
    : state_(rstd::move(other.state_)), thread_(other.thread_.take()), address_(other.address_) {
}
auto Server::operator=(Server&& other) noexcept -> Server& {
    if (this != &other) {
        (void)join();
        state_   = rstd::move(other.state_);
        thread_  = other.thread_.take();
        address_ = other.address_;
    }
    return *this;
}
Server::~Server() {
    (void)join();
}
void Server::stop() const {
    if (state_) state_->stop();
}
auto Server::join() -> rstd::io::Result<empty> {
    stop();
    if (! thread_) return Ok(empty {});
    auto joined = rstd::move(thread_.take().unwrap()).join();
    if (joined.is_err())
        return Err(rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Other }));
    return rstd::move(joined).unwrap();
}
auto Server::start(Handle handle, ServerOptions options) -> rstd::io::Result<Server> {
    if (! options.address.is_loopback() || ! options.max_connections ||
        options.max_connections > 1024 || options.max_message_bytes < 2048 ||
        options.max_message_bytes > 1024 * 1024 || ! options.max_snapshot_bytes ||
        ! options.page_records || options.page_records > 1024 ||
        options.idle_timeout == time::Duration {} || options.snapshot_lifetime == time::Duration {})
        return Err(
            rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::InvalidInput }));
    auto state   = sync::Arc<ServerState>::make(rstd::move(handle), options);
    auto started = thread::builder::Builder::make().spawn(
        [state = state.clone()]() -> rstd::io::Result<empty> {
            auto rt = RuntimeBuilder::current_thread().enable_all().build();
            if (rt.is_err()) {
                startup(state, Err(rstd::move(rt).unwrap_err()));
                return Ok(empty {});
            }
            return rt->block_on(serve(state.clone()));
        });
    if (started.is_err()) return Err(rstd::move(started).unwrap_err());
    auto thread  = rstd::move(started).unwrap();
    auto address = [&]() {
        auto f = state->fields.lock().unwrap();
        while (! f->startup) state->ready.wait(f);
        return f->startup.take().unwrap();
    }();
    if (address.is_err()) {
        state->stop();
        (void)rstd::move(thread).join();
        return Err(rstd::move(address).unwrap_err());
    }
    return Ok(Server(rstd::move(state), rstd::move(thread), *address));
}
} // namespace rstd::async::debug

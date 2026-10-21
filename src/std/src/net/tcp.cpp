module;
#include <rstd/macro.hpp>

module rstd;
import :net.tcp;
import rstd.sys.socket;

using namespace rstd::prelude;
namespace socket = rstd::sys::socket;

inline auto tcp_is_error_kind(rstd::io::Error const&      error,
                              rstd::io::ErrorKind::Entity kind) noexcept -> bool {
    return error.kind() == rstd::io::ErrorKind { kind };
}

inline auto tcp_is_would_block(rstd::io::Error const& error) noexcept -> bool {
    return tcp_is_error_kind(error, rstd::io::ErrorKind::WouldBlock);
}

auto stream_close_source(rstd::async::Registration, rstd::async::CompletionSource)
    -> rstd::async::coro<rstd::io::Result<rstd::empty>>;
auto stream_detach_owned_socket(rstd::async::Registration, rstd::async::CompletionSource)
    -> rstd::async::coro<rstd::io::Result<rstd::os::socket::OwnedSocket>>;

#if RSTD_OS_UNIX
auto stream_detach_owned_fd(rstd::async::coro<rstd::io::Result<rstd::os::socket::OwnedSocket>>)
    -> rstd::async::coro<rstd::io::Result<rstd::os::fd::OwnedFd>>;
#endif

auto tcp_accept(rstd::io::Result<rstd::async::IoOperation> operation) -> rstd::async::coro<
    rstd::io::Result<rstd::tuple<rstd::net::TcpStream, rstd::net::SocketAddr>>> {
    if (operation.is_err()) co_return rstd::Err(rstd::move(operation).unwrap_err());
    auto completed = co_await rstd::move(operation).unwrap();
    if (completed.is_err()) co_return rstd::Err(rstd::move(completed).unwrap_err());
    auto accepted = rstd::move(completed).unwrap().into_socket();
    if (accepted.is_none())
        co_return rstd::Err(
            rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::InvalidData }));
    auto owned = rstd::move(accepted).unwrap();
    auto peer  = socket::peer_addr(owned.as_raw_socket());
    if (peer.is_err()) co_return rstd::Err(rstd::move(peer).unwrap_err());
    auto stream = rstd::net::TcpStream::from_owned_socket(rstd::move(owned));
    if (stream.is_err()) co_return rstd::Err(rstd::move(stream).unwrap_err());
    co_return rstd::Ok(rstd::tuple<rstd::net::TcpStream, rstd::net::SocketAddr> {
        rstd::move(stream).unwrap(), *peer });
}

namespace rstd::net
{

enum class TcpConnectPhase
{
    Fresh,
    Running,
    Connected,
    Failed,
    Closing,
    Consumed
};
struct TcpConnectState {
    sync::Mutex<TcpConnectPhase> phase { TcpConnectPhase::Fresh };
};
struct TcpConnectWork {
    sync::Arc<TcpConnectState> state;
    async::IoOperation         operation;
    TcpConnectWork(sync::Arc<TcpConnectState> state, async::IoOperation operation)
        : state(rstd::move(state)), operation(rstd::move(operation)) {}
    TcpConnectWork(TcpConnectWork&&) noexcept = default;
    ~TcpConnectWork() {
        if (state) {
            auto phase = state->phase.lock().unwrap();
            if (*phase == TcpConnectPhase::Running) *phase = TcpConnectPhase::Failed;
        }
    }
};

auto tcp_connect_failure(io::Error error) -> async::coro<io::Result<empty>> {
    co_return Err(rstd::move(error));
}

auto tcp_finish_connect(TcpConnectWork work) -> async::coro<io::Result<empty>> {
    auto connected = co_await rstd::move(work.operation);
    if (connected.is_err()) co_return Err(rstd::move(connected).unwrap_err());
    auto phase = work.state->phase.lock().unwrap();
    if (*phase != TcpConnectPhase::Running)
        co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
    *phase = TcpConnectPhase::Connected;
    co_return Ok(empty {});
}

auto TcpStream::connect(SocketAddr addr) -> async::coro<io::Result<TcpStream>> {
    auto prepared = prepare_connect(addr);
    if (prepared.is_err()) co_return Err(rstd::move(prepared).unwrap_err());
    auto connecting = rstd::move(prepared).unwrap();
    auto connected  = co_await connecting.finish();
    if (connected.is_err()) co_return Err(rstd::move(connected).unwrap_err());
    co_return rstd::move(connecting).into_stream();
}

auto TcpStream::prepare_connect(SocketAddr addr) -> io::Result<TcpConnect> {
    auto socket_result = socket::tcp(addr);
    if (socket_result.is_err()) {
        return Err(rstd::move(socket_result).unwrap_err_unchecked());
    }

    auto owned_socket  = rstd::move(socket_result).unwrap_unchecked();
    auto stream_result = TcpStream::from_owned_socket(rstd::move(owned_socket));
    if (stream_result.is_err()) {
        return Err(rstd::move(stream_result).unwrap_err_unchecked());
    }

    return Ok(TcpConnect(rstd::move(stream_result).unwrap_unchecked(), addr));
}

TcpConnect::TcpConnect(TcpStream stream, SocketAddr target)
    : stream_(Some(rstd::move(stream))),
      target_(target),
      state_(sync::Arc<TcpConnectState>::make()) {
}
TcpConnect::TcpConnect(TcpConnect&&) noexcept                    = default;
auto TcpConnect::operator=(TcpConnect&&) noexcept -> TcpConnect& = default;
TcpConnect::~TcpConnect()                                        = default;

auto TcpConnect::finish() const -> async::coro<io::Result<empty>> {
    {
        auto phase = state_->phase.lock().unwrap();
        if (*phase != TcpConnectPhase::Fresh || stream_.is_none())
            return tcp_connect_failure(
                io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
        *phase = TcpConnectPhase::Running;
    }
    return tcp_finish_connect(
        TcpConnectWork(state_.clone(),
                       async::IoOperation::connect(stream_->stream_.m_completion_source, target_)));
}

auto TcpConnect::into_stream() && -> io::Result<TcpStream> {
    auto phase = state_->phase.lock().unwrap();
    if (*phase != TcpConnectPhase::Connected || stream_.is_none())
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
    *phase = TcpConnectPhase::Consumed;
    return Ok(stream_.take().unwrap());
}

auto TcpConnect::close() && -> async::coro<io::Result<empty>> {
    {
        auto phase = state_->phase.lock().unwrap();
        if (stream_.is_none())
            return tcp_connect_failure(
                io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        *phase = TcpConnectPhase::Closing;
    }
    return rstd::move(stream_.take().unwrap()).close();
}

auto TcpConnect::local_addr() const -> io::Result<SocketAddr> {
    if (stream_.is_none())
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
    return stream_->local_addr();
}

auto TcpStream::from_owned_socket(os::socket::OwnedSocket socket) -> io::Result<TcpStream> {
    auto stream = SocketStream::from_owned_socket(rstd::move(socket));
    if (stream.is_err()) return Err(rstd::move(stream).unwrap_err());
    return Ok(TcpStream(rstd::move(stream).unwrap()));
}
#if RSTD_OS_UNIX
auto TcpStream::from_owned_fd(os::fd::OwnedFd fd) -> io::Result<TcpStream> {
    return from_owned_socket(
        os::socket::OwnedSocket::from_raw_socket(rstd::move(fd).into_raw_fd()));
}
#endif
auto TcpStream::local_addr() const -> io::Result<SocketAddr> {
    return socket::local_addr(stream_.m_completion_source.as_raw_fd());
}
auto TcpStream::peer_addr() const -> io::Result<SocketAddr> {
    return socket::peer_addr(stream_.m_completion_source.as_raw_fd());
}
auto TcpStream::set_nodelay(bool enabled) -> io::Result<empty> {
    return socket::set_nodelay(stream_.m_completion_source.as_raw_fd(), enabled);
}

auto TcpListener::bind(SocketAddr addr) -> io::Result<TcpListener> {
    auto socket_result = socket::tcp(addr);
    if (socket_result.is_err()) return Err(rstd::move(socket_result).unwrap_err_unchecked());

    auto owned_socket = rstd::move(socket_result).unwrap_unchecked();
    auto reuse        = socket::set_reuseaddr(owned_socket.as_raw_socket(), true);
    if (reuse.is_err()) return Err(rstd::move(reuse).unwrap_err_unchecked());

    auto bound = socket::bind(owned_socket.as_raw_socket(), addr);
    if (bound.is_err()) return Err(rstd::move(bound).unwrap_err_unchecked());

    auto listening = socket::listen(owned_socket.as_raw_socket());
    if (listening.is_err()) return Err(rstd::move(listening).unwrap_err_unchecked());

    auto registration = async::Registration::register_fd(owned_socket.as_raw_socket());
    if (registration.is_err()) return Err(rstd::move(registration).unwrap_err_unchecked());

    return Ok(
        TcpListener { rstd::move(owned_socket), rstd::move(registration).unwrap_unchecked() });
}

auto TcpListener::from_owned_socket(os::socket::OwnedSocket owned_socket)
    -> io::Result<TcpListener> {
    auto nonblocking = socket::set_nonblocking(owned_socket.as_raw_socket(), true);
    if (nonblocking.is_err()) return Err(rstd::move(nonblocking).unwrap_err_unchecked());

    auto registration = async::Registration::register_fd(owned_socket.as_raw_socket());
    if (registration.is_err()) return Err(rstd::move(registration).unwrap_err_unchecked());
    return Ok(
        TcpListener { rstd::move(owned_socket), rstd::move(registration).unwrap_unchecked() });
}

auto TcpListener::into_owned_socket() && -> async::coro<io::Result<os::socket::OwnedSocket>> {
    return stream_detach_owned_socket(rstd::move(m_registration), rstd::move(m_completion_source));
}

auto TcpListener::close() && -> async::coro<io::Result<empty>> {
    return stream_close_source(rstd::move(m_registration), rstd::move(m_completion_source));
}

#if RSTD_OS_UNIX
auto TcpListener::from_owned_fd(os::fd::OwnedFd fd) -> io::Result<TcpListener> {
    return from_owned_socket(
        os::socket::OwnedSocket::from_raw_socket(rstd::move(fd).into_raw_fd()));
}

auto TcpListener::into_owned_fd() && -> async::coro<io::Result<os::fd::OwnedFd>> {
    return stream_detach_owned_fd(rstd::move(*this).into_owned_socket());
}
#endif

auto TcpListener::local_addr() const -> io::Result<SocketAddr> {
    return socket::local_addr(m_completion_source.as_raw_fd());
}

auto TcpListener::try_accept() -> io::Result<tuple<TcpStream, SocketAddr>> {
    auto accepted = socket::accept(m_completion_source.as_raw_fd());
    if (accepted.is_err()) {
        auto error = rstd::move(accepted).unwrap_err_unchecked();
        if (tcp_is_would_block(error)) {
            m_registration.clear_readiness(async::Ready::readable());
        }
        return Err(rstd::move(error));
    }

    auto accepted_tuple  = rstd::move(accepted).unwrap_unchecked();
    auto accepted_socket = rstd::move(accepted_tuple.template get<0>());
    auto addr            = rstd::move(accepted_tuple.template get<1>());

    auto stream = TcpStream::from_owned_socket(rstd::move(accepted_socket));
    if (stream.is_err()) return Err(rstd::move(stream).unwrap_err_unchecked());

    return Ok(tuple<TcpStream, SocketAddr> {
        rstd::move(stream).unwrap_unchecked(),
        rstd::move(addr),
    });
}

auto TcpListener::accept() -> async::coro<io::Result<tuple<TcpStream, SocketAddr>>> {
    auto address = local_addr();
    if (address.is_err()) return tcp_accept(Err(rstd::move(address).unwrap_err()));
    auto prepared = socket::prepare_accept(*address);
    if (prepared.is_err()) return tcp_accept(Err(rstd::move(prepared).unwrap_err()));
    return tcp_accept(Ok(async::IoOperation::accept(m_completion_source,
                                                    *address,
                                                    rstd::move(prepared).unwrap(),
                                                    socket::accept_address_buffer_size())));
}

} // namespace rstd::net

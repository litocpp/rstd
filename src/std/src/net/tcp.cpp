module;
#include <rstd/macro.hpp>

module rstd;
import :net.tcp;
import :sys.socket;

using namespace rstd::prelude;
namespace socket = rstd::sys::socket;

inline auto tcp_is_error_kind(rstd::io::Error const&      error,
                              rstd::io::ErrorKind::Entity kind) noexcept -> bool {
    return error.kind() == rstd::io::ErrorKind { kind };
}

inline auto tcp_is_would_block(rstd::io::Error const& error) noexcept -> bool {
    return tcp_is_error_kind(error, rstd::io::ErrorKind::WouldBlock);
}

auto tcp_detach_owned_socket(rstd::os::socket::OwnedSocket socket,
                             rstd::async::Registration     registration,
                             rstd::async::CompletionSource completion_source)
    -> rstd::async::coro<rstd::io::Result<rstd::os::socket::OwnedSocket>> {
    auto deregistered = co_await rstd::move(registration).deregister();
    if (deregistered.is_err()) {
        co_return rstd::Err(rstd::move(deregistered).unwrap_err_unchecked());
    }
    auto released = co_await rstd::move(completion_source).release();
    if (released.is_err()) {
        co_return rstd::Err(rstd::move(released).unwrap_err_unchecked());
    }
    co_return rstd::Ok(rstd::move(socket));
}

namespace rstd::net
{

auto TcpStream::connect(SocketAddr addr) -> async::coro<io::Result<TcpStream>> {
    auto socket_result = socket::tcp(addr);
    if (socket_result.is_err()) {
        co_return Err(rstd::move(socket_result).unwrap_err_unchecked());
    }

    auto owned_socket  = rstd::move(socket_result).unwrap_unchecked();
    auto stream_result = TcpStream::from_owned_socket(rstd::move(owned_socket));
    if (stream_result.is_err()) {
        co_return Err(rstd::move(stream_result).unwrap_err_unchecked());
    }

    auto stream    = rstd::move(stream_result).unwrap_unchecked();
    auto connected = co_await async::IoOperation::connect(stream.m_completion_source, addr);
    if (connected.is_err()) co_return Err(rstd::move(connected).unwrap_err_unchecked());

    co_return Ok(rstd::move(stream));
}

auto TcpStream::from_owned_socket(os::socket::OwnedSocket owned_socket) -> io::Result<TcpStream> {
    auto nonblocking = socket::set_nonblocking(owned_socket.as_raw_socket(), true);
    if (nonblocking.is_err()) return Err(rstd::move(nonblocking).unwrap_err_unchecked());

    auto registration = async::Registration::register_fd(owned_socket.as_raw_socket());
    if (registration.is_err()) return Err(rstd::move(registration).unwrap_err_unchecked());
    return Ok(TcpStream { rstd::move(owned_socket), rstd::move(registration).unwrap_unchecked() });
}

auto TcpStream::into_owned_socket() && -> async::coro<io::Result<os::socket::OwnedSocket>> {
#if RSTD_OS_WINDOWS
    if (m_completion_source.is_bound()) {
        co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
    }
#endif
    m_read_operation  = None<async::IoOperation>();
    m_write_operation = None<async::IoOperation>();
    co_return co_await tcp_detach_owned_socket(
        rstd::move(m_socket), rstd::move(m_registration), rstd::move(m_completion_source));
}

#if RSTD_OS_UNIX
auto TcpStream::from_owned_fd(os::fd::OwnedFd fd) -> io::Result<TcpStream> {
    return from_owned_socket(
        os::socket::OwnedSocket::from_raw_socket(rstd::move(fd).into_raw_fd()));
}

auto TcpStream::into_owned_fd() && -> async::coro<io::Result<os::fd::OwnedFd>> {
    auto detached = co_await rstd::move(*this).into_owned_socket();
    if (detached.is_err()) co_return Err(rstd::move(detached).unwrap_err_unchecked());
    auto owned_socket = rstd::move(detached).unwrap_unchecked();
    co_return Ok(os::fd::OwnedFd::from_raw_fd(rstd::move(owned_socket).into_raw_socket()));
}
#endif

auto TcpStream::local_addr() const -> io::Result<SocketAddr> {
    return socket::local_addr(m_socket.as_raw_socket());
}

auto TcpStream::peer_addr() const -> io::Result<SocketAddr> {
    return socket::peer_addr(m_socket.as_raw_socket());
}

auto TcpStream::take_error() -> io::Result<Option<io::Error>> {
    return socket::take_error(m_socket.as_raw_socket());
}

auto TcpStream::set_nodelay(bool enabled) -> io::Result<empty> {
    return socket::set_nodelay(m_socket.as_raw_socket(), enabled);
}

auto TcpStream::shutdown() -> io::Result<empty> {
    return socket::shutdown_write(m_socket.as_raw_socket());
}

auto TcpStream::try_read(bytes::BytesMut& buf) -> io::Result<usize> {
    auto chunk  = buf.chunk_mut();
    auto result = socket::recv(m_socket.as_raw_socket(), as_bytes_mut(chunk));
    if (result.is_ok()) {
        auto n = rstd::move(result).unwrap_unchecked();
        buf.advance_mut(n);
        return Ok(n);
    }

    auto error = rstd::move(result).unwrap_err_unchecked();
    if (tcp_is_would_block(error)) {
        m_registration.clear_readiness(async::Ready::readable());
    }
    return Err(rstd::move(error));
}

auto TcpStream::try_write(bytes::Bytes const& buf) -> io::Result<usize> {
    auto result = socket::send(m_socket.as_raw_socket(), as_bytes(buf.as_slice()));
    if (result.is_ok()) return result;

    auto error = rstd::move(result).unwrap_err_unchecked();
    if (tcp_is_would_block(error)) {
        m_registration.clear_readiness(async::Ready::writable());
    }
    return Err(rstd::move(error));
}

auto TcpStream::poll_read(mut_ref<TcpStream> self, task::Context& cx, bytes::BytesMut& buf)
    -> task::Poll<io::Result<usize>> {
    auto& stream = *self;
    if (stream.m_read_operation.is_none()) {
        stream.m_read_operation =
            Some(async::IoOperation::read(stream.m_completion_source, buf.chunk_mut().len()));
    }

    auto polled = future::poll(*stream.m_read_operation, cx);
    if (polled.is_pending()) return task::Poll<io::Result<usize>>::Pending();
    stream.m_read_operation = None<async::IoOperation>();

    auto result = rstd::move(polled).take();
    if (result.is_err()) {
        return task::Poll<io::Result<usize>>::Ready(Err(rstd::move(result).unwrap_err_unchecked()));
    }
    auto completion  = rstd::move(result).unwrap_unchecked();
    auto transferred = completion.transferred();
    buf.put_slice(completion.data());
    return task::Poll<io::Result<usize>>::Ready(Ok(transferred));
}

auto TcpStream::poll_write(mut_ref<TcpStream> self, task::Context& cx, bytes::Bytes const& buf)
    -> task::Poll<io::Result<usize>> {
    auto& stream = *self;
    if (stream.m_write_operation.is_none()) {
        stream.m_write_operation = Some(async::IoOperation::write(
            stream.m_completion_source, bytes::Bytes::copy_from_slice(buf.as_slice())));
    }

    auto polled = future::poll(*stream.m_write_operation, cx);
    if (polled.is_pending()) return task::Poll<io::Result<usize>>::Pending();
    stream.m_write_operation = None<async::IoOperation>();

    auto result = rstd::move(polled).take();
    if (result.is_err()) {
        return task::Poll<io::Result<usize>>::Ready(Err(rstd::move(result).unwrap_err_unchecked()));
    }
    return task::Poll<io::Result<usize>>::Ready(
        Ok(rstd::move(result).unwrap_unchecked().transferred()));
}

auto TcpStream::poll_flush(mut_ref<TcpStream>, task::Context&) -> task::Poll<io::Result<empty>> {
    return task::Poll<io::Result<empty>>::Ready(Ok(empty {}));
}

auto TcpStream::poll_shutdown(mut_ref<TcpStream> self, task::Context&)
    -> task::Poll<io::Result<empty>> {
    return task::Poll<io::Result<empty>>::Ready(self->shutdown());
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
#if RSTD_OS_WINDOWS
    if (m_completion_source.is_bound()) {
        co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
    }
#endif
    co_return co_await tcp_detach_owned_socket(
        rstd::move(m_socket), rstd::move(m_registration), rstd::move(m_completion_source));
}

#if RSTD_OS_UNIX
auto TcpListener::from_owned_fd(os::fd::OwnedFd fd) -> io::Result<TcpListener> {
    return from_owned_socket(
        os::socket::OwnedSocket::from_raw_socket(rstd::move(fd).into_raw_fd()));
}

auto TcpListener::into_owned_fd() && -> async::coro<io::Result<os::fd::OwnedFd>> {
    auto detached = co_await rstd::move(*this).into_owned_socket();
    if (detached.is_err()) co_return Err(rstd::move(detached).unwrap_err_unchecked());
    auto owned_socket = rstd::move(detached).unwrap_unchecked();
    co_return Ok(os::fd::OwnedFd::from_raw_fd(rstd::move(owned_socket).into_raw_socket()));
}
#endif

auto TcpListener::local_addr() const -> io::Result<SocketAddr> {
    return socket::local_addr(m_socket.as_raw_socket());
}

auto TcpListener::try_accept() -> io::Result<tuple<TcpStream, SocketAddr>> {
    auto accepted = socket::accept(m_socket.as_raw_socket());
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
    auto listener_addr = local_addr();
    if (listener_addr.is_err()) {
        co_return Err(rstd::move(listener_addr).unwrap_err_unchecked());
    }
    auto address  = rstd::move(listener_addr).unwrap_unchecked();
    auto prepared = socket::prepare_accept(address);
    if (prepared.is_err()) co_return Err(rstd::move(prepared).unwrap_err_unchecked());
    auto operation = async::IoOperation::accept(m_completion_source,
                                                address,
                                                rstd::move(prepared).unwrap_unchecked(),
                                                socket::accept_address_buffer_size());

    auto completed = co_await rstd::move(operation);
    if (completed.is_err()) co_return Err(rstd::move(completed).unwrap_err_unchecked());
    auto accepted = rstd::move(completed).unwrap_unchecked().into_socket();
    if (accepted.is_none()) {
        co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidData }));
    }
    auto accepted_socket = rstd::move(accepted).unwrap_unchecked();
    auto peer            = socket::peer_addr(accepted_socket.as_raw_socket());
    if (peer.is_err()) co_return Err(rstd::move(peer).unwrap_err_unchecked());
    auto stream = TcpStream::from_owned_socket(rstd::move(accepted_socket));
    if (stream.is_err()) co_return Err(rstd::move(stream).unwrap_err_unchecked());
    co_return Ok(tuple<TcpStream, SocketAddr> { rstd::move(stream).unwrap_unchecked(),
                                                rstd::move(peer).unwrap_unchecked() });
}

} // namespace rstd::net

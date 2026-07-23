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

inline auto tcp_is_in_progress(rstd::io::Error const& error) noexcept -> bool {
    return tcp_is_error_kind(error, rstd::io::ErrorKind::InProgress);
}

auto tcp_detach_owned_fd(rstd::os::fd::OwnedFd fd, rstd::async::Registration registration)
    -> rstd::async::coro<rstd::io::Result<rstd::os::fd::OwnedFd>> {
    auto deregistered = co_await rstd::move(registration).deregister();
    if (deregistered.is_err()) {
        co_return rstd::Err(rstd::move(deregistered).unwrap_err_unchecked());
    }
    co_return rstd::Ok(rstd::move(fd));
}

namespace rstd::net
{

auto TcpStream::connect(SocketAddr addr) -> async::coro<io::Result<TcpStream>> {
    auto fd_result = socket::tcp(addr);
    if (fd_result.is_err()) {
        co_return Err(rstd::move(fd_result).unwrap_err_unchecked());
    }

    auto fd             = rstd::move(fd_result).unwrap_unchecked();
    bool connecting     = false;
    auto connect_result = socket::connect(fd.as_raw_fd(), addr);
    if (connect_result.is_err()) {
        auto error = rstd::move(connect_result).unwrap_err_unchecked();
        if (! tcp_is_in_progress(error)) {
            co_return Err(rstd::move(error));
        }
        connecting = true;
    }

    auto stream_result = TcpStream::from_owned_fd(rstd::move(fd));
    if (stream_result.is_err()) {
        co_return Err(rstd::move(stream_result).unwrap_err_unchecked());
    }

    auto stream = rstd::move(stream_result).unwrap_unchecked();
    if (connecting) {
        auto ready = co_await stream.writable();
        if (ready.is_err()) co_return Err(rstd::move(ready).unwrap_err_unchecked());

        auto socket_error = socket::take_error(stream.m_fd.as_raw_fd());
        if (socket_error.is_err()) {
            co_return Err(rstd::move(socket_error).unwrap_err_unchecked());
        }
        auto error = rstd::move(socket_error).unwrap_unchecked();
        if (error.is_some()) {
            co_return Err(rstd::move(error).unwrap_unchecked());
        }
    }

    co_return Ok(rstd::move(stream));
}

auto TcpStream::from_owned_fd(os::fd::OwnedFd fd) -> io::Result<TcpStream> {
    auto nonblocking = socket::set_nonblocking(fd.as_raw_fd(), true);
    if (nonblocking.is_err()) return Err(rstd::move(nonblocking).unwrap_err_unchecked());

    auto registration = async::Registration::register_fd(fd.as_raw_fd());
    if (registration.is_err()) return Err(rstd::move(registration).unwrap_err_unchecked());
    return Ok(TcpStream { rstd::move(fd), rstd::move(registration).unwrap_unchecked() });
}

auto TcpStream::into_owned_fd() && -> async::coro<io::Result<os::fd::OwnedFd>> {
    return tcp_detach_owned_fd(rstd::move(m_fd), rstd::move(m_registration));
}

auto TcpStream::local_addr() const -> io::Result<SocketAddr> {
    return socket::local_addr(m_fd.as_raw_fd());
}

auto TcpStream::peer_addr() const -> io::Result<SocketAddr> {
    return socket::peer_addr(m_fd.as_raw_fd());
}

auto TcpStream::take_error() -> io::Result<Option<io::Error>> {
    return socket::take_error(m_fd.as_raw_fd());
}

auto TcpStream::set_nodelay(bool enabled) -> io::Result<empty> {
    return socket::set_nodelay(m_fd.as_raw_fd(), enabled);
}

auto TcpStream::shutdown() -> io::Result<empty> {
    return socket::shutdown_write(m_fd.as_raw_fd());
}

auto TcpStream::try_read(bytes::BytesMut& buf) -> io::Result<usize> {
    auto chunk  = buf.chunk_mut();
    auto result = socket::recv(m_fd.as_raw_fd(), as_bytes_mut(chunk));
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
    auto result = socket::send(m_fd.as_raw_fd(), as_bytes(buf.as_slice()));
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
    auto  event  = Option<async::ReadyEvent> {};
    while (true) {
        auto chunk  = buf.chunk_mut();
        auto result = socket::recv(stream.m_fd.as_raw_fd(), as_bytes_mut(chunk));
        if (result.is_ok()) {
            auto n = rstd::move(result).unwrap_unchecked();
            buf.advance_mut(n);
            return task::Poll<io::Result<usize>>::Ready(Ok(n));
        }

        auto error = rstd::move(result).unwrap_err_unchecked();
        if (! tcp_is_would_block(error)) {
            return task::Poll<io::Result<usize>>::Ready(Err(rstd::move(error)));
        }

        if (event.is_some()) {
            auto previous = event.take();
            stream.m_registration.clear_readiness(rstd::move(previous).unwrap_unchecked());
        }

        auto ready = stream.m_registration.poll_readiness(
            cx, async::Interest::readable(), stream.m_read_waiter_id);
        if (ready.is_pending()) return task::Poll<io::Result<usize>>::Pending();

        stream.m_read_waiter_id = usize();
        auto ready_result       = rstd::move(ready).take();
        if (ready_result.is_err()) {
            return task::Poll<io::Result<usize>>::Ready(
                Err(rstd::move(ready_result).unwrap_err_unchecked()));
        }
        event.insert(rstd::move(ready_result).unwrap_unchecked());
    }
}

auto TcpStream::poll_write(mut_ref<TcpStream> self, task::Context& cx, bytes::Bytes const& buf)
    -> task::Poll<io::Result<usize>> {
    auto& stream = *self;
    auto  event  = Option<async::ReadyEvent> {};
    while (true) {
        auto result = socket::send(stream.m_fd.as_raw_fd(), as_bytes(buf.as_slice()));
        if (result.is_ok()) {
            return task::Poll<io::Result<usize>>::Ready(rstd::move(result));
        }

        auto error = rstd::move(result).unwrap_err_unchecked();
        if (! tcp_is_would_block(error)) {
            return task::Poll<io::Result<usize>>::Ready(Err(rstd::move(error)));
        }

        if (event.is_some()) {
            auto previous = event.take();
            stream.m_registration.clear_readiness(rstd::move(previous).unwrap_unchecked());
        }

        auto ready = stream.m_registration.poll_readiness(
            cx, async::Interest::writable(), stream.m_write_waiter_id);
        if (ready.is_pending()) return task::Poll<io::Result<usize>>::Pending();

        stream.m_write_waiter_id = usize();
        auto ready_result        = rstd::move(ready).take();
        if (ready_result.is_err()) {
            return task::Poll<io::Result<usize>>::Ready(
                Err(rstd::move(ready_result).unwrap_err_unchecked()));
        }
        event.insert(rstd::move(ready_result).unwrap_unchecked());
    }
}

auto TcpStream::poll_flush(mut_ref<TcpStream>, task::Context&) -> task::Poll<io::Result<empty>> {
    return task::Poll<io::Result<empty>>::Ready(Ok(empty {}));
}

auto TcpStream::poll_shutdown(mut_ref<TcpStream> self, task::Context&)
    -> task::Poll<io::Result<empty>> {
    return task::Poll<io::Result<empty>>::Ready(self->shutdown());
}

auto TcpListener::bind(SocketAddr addr) -> io::Result<TcpListener> {
    auto fd_result = socket::tcp(addr);
    if (fd_result.is_err()) return Err(rstd::move(fd_result).unwrap_err_unchecked());

    auto fd    = rstd::move(fd_result).unwrap_unchecked();
    auto reuse = socket::set_reuseaddr(fd.as_raw_fd(), true);
    if (reuse.is_err()) return Err(rstd::move(reuse).unwrap_err_unchecked());

    auto bound = socket::bind(fd.as_raw_fd(), addr);
    if (bound.is_err()) return Err(rstd::move(bound).unwrap_err_unchecked());

    auto listening = socket::listen(fd.as_raw_fd());
    if (listening.is_err()) return Err(rstd::move(listening).unwrap_err_unchecked());

    auto registration = async::Registration::register_fd(fd.as_raw_fd());
    if (registration.is_err()) return Err(rstd::move(registration).unwrap_err_unchecked());

    return Ok(TcpListener { rstd::move(fd), rstd::move(registration).unwrap_unchecked() });
}

auto TcpListener::from_owned_fd(os::fd::OwnedFd fd) -> io::Result<TcpListener> {
    auto nonblocking = socket::set_nonblocking(fd.as_raw_fd(), true);
    if (nonblocking.is_err()) return Err(rstd::move(nonblocking).unwrap_err_unchecked());

    auto registration = async::Registration::register_fd(fd.as_raw_fd());
    if (registration.is_err()) return Err(rstd::move(registration).unwrap_err_unchecked());
    return Ok(TcpListener { rstd::move(fd), rstd::move(registration).unwrap_unchecked() });
}

auto TcpListener::into_owned_fd() && -> async::coro<io::Result<os::fd::OwnedFd>> {
    return tcp_detach_owned_fd(rstd::move(m_fd), rstd::move(m_registration));
}

auto TcpListener::local_addr() const -> io::Result<SocketAddr> {
    return socket::local_addr(m_fd.as_raw_fd());
}

auto TcpListener::try_accept() -> io::Result<tuple<TcpStream, SocketAddr>> {
    auto accepted = socket::accept(m_fd.as_raw_fd());
    if (accepted.is_err()) {
        auto error = rstd::move(accepted).unwrap_err_unchecked();
        if (tcp_is_would_block(error)) {
            m_registration.clear_readiness(async::Ready::readable());
        }
        return Err(rstd::move(error));
    }

    auto accepted_tuple = rstd::move(accepted).unwrap_unchecked();
    auto fd             = rstd::move(accepted_tuple.template get<0>());
    auto addr           = rstd::move(accepted_tuple.template get<1>());

    auto stream = TcpStream::from_owned_fd(rstd::move(fd));
    if (stream.is_err()) return Err(rstd::move(stream).unwrap_err_unchecked());

    return Ok(tuple<TcpStream, SocketAddr> {
        rstd::move(stream).unwrap_unchecked(),
        rstd::move(addr),
    });
}

auto TcpListener::accept() -> async::coro<io::Result<tuple<TcpStream, SocketAddr>>> {
    auto event = Option<async::ReadyEvent> {};
    while (true) {
        auto accepted = socket::accept(m_fd.as_raw_fd());
        if (accepted.is_ok()) {
            auto accepted_tuple = rstd::move(accepted).unwrap_unchecked();
            auto fd             = rstd::move(accepted_tuple.template get<0>());
            auto addr           = rstd::move(accepted_tuple.template get<1>());

            auto stream = TcpStream::from_owned_fd(rstd::move(fd));
            if (stream.is_err()) co_return Err(rstd::move(stream).unwrap_err_unchecked());

            co_return Ok(tuple<TcpStream, SocketAddr> {
                rstd::move(stream).unwrap_unchecked(),
                rstd::move(addr),
            });
        }

        auto error = rstd::move(accepted).unwrap_err_unchecked();
        if (! tcp_is_would_block(error)) {
            co_return Err(rstd::move(error));
        }

        if (event.is_some()) {
            auto previous = event.take();
            m_registration.clear_readiness(rstd::move(previous).unwrap_unchecked());
        }

        auto ready = co_await readable();
        if (ready.is_err()) {
            co_return Err(rstd::move(ready).unwrap_err_unchecked());
        }
        event.insert(rstd::move(ready).unwrap_unchecked());
    }
}

} // namespace rstd::net

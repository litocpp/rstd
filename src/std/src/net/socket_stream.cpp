module;
#include <rstd/macro.hpp>
module rstd;
import :net.socket_stream;
import rstd.sys.socket;
using namespace rstd::prelude;
namespace socket = rstd::sys::socket;
inline auto stream_is_error_kind(rstd::io::Error const&      error,
                                 rstd::io::ErrorKind::Entity kind) noexcept -> bool {
    return error.kind() == rstd::io::ErrorKind { kind };
}

inline auto stream_is_would_block(rstd::io::Error const& error) noexcept -> bool {
    return stream_is_error_kind(error, rstd::io::ErrorKind::WouldBlock);
}

auto stream_release_source(rstd::async::Registration     registration,
                           rstd::async::CompletionSource source)
    -> rstd::async::coro<rstd::io::Result<rstd::empty>> {
    auto deregistered = co_await rstd::move(registration).deregister();
    auto released     = co_await rstd::move(source).release();
    if (deregistered.is_err()) co_return rstd::Err(rstd::move(deregistered).unwrap_err());
    co_return released;
}

auto stream_close_source(rstd::async::Registration     registration,
                         rstd::async::CompletionSource source)
    -> rstd::async::coro<rstd::io::Result<rstd::empty>> {
    auto retained = source.clone();
    auto released = co_await stream_release_source(rstd::move(registration), rstd::move(source));
    auto owned    = retained.take_owned_socket();
    if (owned.is_err()) {
        if (released.is_err()) co_return rstd::Err(rstd::move(released).unwrap_err());
        co_return rstd::Err(rstd::move(owned).unwrap_err());
    }
    {
        auto closing = rstd::move(owned).unwrap();
    }
    co_return released;
}

auto stream_detach_owned_socket(rstd::async::Registration     registration,
                                rstd::async::CompletionSource source)
    -> rstd::async::coro<rstd::io::Result<rstd::os::socket::OwnedSocket>> {
#if RSTD_OS_WINDOWS
    if (source.is_bound())
        co_return rstd::Err(
            rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::Unsupported }));
#endif
    auto retained = source.clone();
    auto released = co_await stream_release_source(rstd::move(registration), rstd::move(source));
    if (released.is_err()) co_return rstd::Err(rstd::move(released).unwrap_err());
    co_return retained.take_owned_socket();
}

#if RSTD_OS_UNIX
auto stream_detach_owned_fd(
    rstd::async::coro<rstd::io::Result<rstd::os::socket::OwnedSocket>> pending)
    -> rstd::async::coro<rstd::io::Result<rstd::os::fd::OwnedFd>> {
    auto detached = co_await rstd::move(pending);
    if (detached.is_err()) co_return rstd::Err(rstd::move(detached).unwrap_err());
    co_return rstd::Ok(
        rstd::os::fd::OwnedFd::from_raw_fd(rstd::move(detached).unwrap().into_raw_socket()));
}
#endif

auto stream_receive(rstd::io::Result<rstd::async::IoOperation> operation)
    -> rstd::async::coro<rstd::io::Result<rstd::bytes::Bytes>> {
    if (operation.is_err()) co_return rstd::Err(rstd::move(operation).unwrap_err());
    auto result = co_await rstd::move(operation).unwrap();
    if (result.is_err()) co_return rstd::Err(rstd::move(result).unwrap_err());
    co_return rstd::Ok(rstd::move(result).unwrap().into_data());
}

auto stream_send(rstd::async::IoOperation operation)
    -> rstd::async::coro<rstd::io::Result<rstd::usize>> {
    auto result = co_await rstd::move(operation);
    if (result.is_err()) co_return rstd::Err(rstd::move(result).unwrap_err());
    co_return rstd::Ok(result->transferred());
}

auto stream_send_all(rstd::async::CompletionSource source, rstd::bytes::Bytes data)
    -> rstd::async::coro<rstd::io::Result<rstd::usize>> {
    auto length = data.len();
    while (! data.is_empty()) {
        auto result = co_await rstd::async::IoOperation::write(source, data.clone());
        if (result.is_err()) co_return rstd::Err(rstd::move(result).unwrap_err());
        auto count = result->transferred();
        if (count == rstd::usize())
            co_return rstd::Err(
                rstd::io::Error::from_kind(rstd::io::ErrorKind { rstd::io::ErrorKind::WriteZero }));
        data.advance(count);
    }
    co_return rstd::Ok(length);
}

namespace rstd::net
{
auto SocketStream::from_owned_socket(os::socket::OwnedSocket owned_socket)
    -> io::Result<SocketStream> {
    auto nonblocking = socket::set_nonblocking(owned_socket.as_raw_socket(), true);
    if (nonblocking.is_err()) return Err(rstd::move(nonblocking).unwrap_err_unchecked());

    auto registration = async::Registration::register_fd(owned_socket.as_raw_socket());
    if (registration.is_err()) return Err(rstd::move(registration).unwrap_err_unchecked());
    return Ok(
        SocketStream { rstd::move(owned_socket), rstd::move(registration).unwrap_unchecked() });
}

auto SocketStream::into_owned_socket() && -> async::coro<io::Result<os::socket::OwnedSocket>> {
    m_read_ready  = None();
    m_write_ready = None();
    return stream_detach_owned_socket(rstd::move(m_registration), rstd::move(m_completion_source));
}

auto SocketStream::close() && -> async::coro<io::Result<empty>> {
    m_read_ready  = None();
    m_write_ready = None();
    return stream_close_source(rstd::move(m_registration), rstd::move(m_completion_source));
}

auto SocketStream::recv(usize maximum_size) const -> async::coro<io::Result<bytes::Bytes>> {
    if (maximum_size == usize())
        return stream_receive(
            Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput })));
    return stream_receive(Ok(async::IoOperation::read(m_completion_source, maximum_size)));
}

auto SocketStream::send(bytes::Bytes data) const -> async::coro<io::Result<usize>> {
    return stream_send(async::IoOperation::write(m_completion_source, rstd::move(data)));
}

auto SocketStream::send_all(bytes::Bytes data) const -> async::coro<io::Result<usize>> {
    return stream_send_all(m_completion_source.clone(), rstd::move(data));
}

#if RSTD_OS_UNIX
auto SocketStream::from_owned_fd(os::fd::OwnedFd fd) -> io::Result<SocketStream> {
    return from_owned_socket(
        os::socket::OwnedSocket::from_raw_socket(rstd::move(fd).into_raw_fd()));
}

auto SocketStream::into_owned_fd() && -> async::coro<io::Result<os::fd::OwnedFd>> {
    return stream_detach_owned_fd(rstd::move(*this).into_owned_socket());
}
#endif

auto SocketStream::take_error() -> io::Result<Option<io::Error>> {
    return socket::take_error(m_completion_source.as_raw_fd());
}

auto SocketStream::shutdown() -> io::Result<empty> {
    return socket::shutdown_write(m_completion_source.as_raw_fd());
}

auto SocketStream::shutdown_both() -> io::Result<empty> {
    return socket::shutdown_both(m_completion_source.as_raw_fd());
}

auto SocketStream::try_read(bytes::BytesMut& buf) -> io::Result<usize> {
    auto chunk  = buf.chunk_mut();
    auto result = socket::recv(m_completion_source.as_raw_fd(), as_bytes_mut(chunk));
    if (result.is_ok()) {
        auto n = rstd::move(result).unwrap_unchecked();
        buf.advance_mut(n);
        return Ok(n);
    }

    auto error = rstd::move(result).unwrap_err_unchecked();
    if (stream_is_would_block(error)) {
        m_registration.clear_readiness(async::Ready::readable());
    }
    return Err(rstd::move(error));
}

auto SocketStream::try_write(bytes::Bytes const& buf) -> io::Result<usize> {
    auto result = socket::send(m_completion_source.as_raw_fd(), as_bytes(buf.as_slice()));
    if (result.is_ok()) return result;

    auto error = rstd::move(result).unwrap_err_unchecked();
    if (stream_is_would_block(error)) {
        m_registration.clear_readiness(async::Ready::writable());
    }
    return Err(rstd::move(error));
}

auto SocketStream::poll_read(mut_ref<SocketStream> self, task::Context& cx, bytes::BytesMut& buf)
    -> task::Poll<io::Result<usize>> {
    auto& stream = *self;
    while (true) {
        if (stream.m_read_ready.is_none()) stream.m_read_ready = Some(stream.readable());
        auto ready = future::poll(*stream.m_read_ready, cx);
        if (ready.is_pending()) return task::Poll<io::Result<usize>>::Pending();
        stream.m_read_ready = None();
        auto event          = rstd::move(ready).take();
        if (event.is_err())
            return task::Poll<io::Result<usize>>::Ready(Err(rstd::move(event).unwrap_err()));
        auto chunk  = buf.chunk_mut();
        auto result = socket::recv(stream.m_completion_source.as_raw_fd(), as_bytes_mut(chunk));
        if (result.is_ok()) {
            buf.advance_mut(*result);
            return task::Poll<io::Result<usize>>::Ready(rstd::move(result));
        }
        if (! stream_is_would_block(result.unwrap_err()))
            return task::Poll<io::Result<usize>>::Ready(rstd::move(result));
        stream.m_registration.clear_readiness(*event);
    }
}

auto SocketStream::poll_write(mut_ref<SocketStream> self,
                              task::Context&        cx,
                              bytes::Bytes const&   buf) -> task::Poll<io::Result<usize>> {
    if (buf.is_empty()) return task::Poll<io::Result<usize>>::Ready(Ok(usize()));
    auto& stream = *self;
    while (true) {
        if (stream.m_write_ready.is_none()) stream.m_write_ready = Some(stream.writable());
        auto ready = future::poll(*stream.m_write_ready, cx);
        if (ready.is_pending()) return task::Poll<io::Result<usize>>::Pending();
        stream.m_write_ready = None();
        auto event           = rstd::move(ready).take();
        if (event.is_err())
            return task::Poll<io::Result<usize>>::Ready(Err(rstd::move(event).unwrap_err()));
        auto result =
            socket::send(stream.m_completion_source.as_raw_fd(), as_bytes(buf.as_slice()));
        if (result.is_ok() || ! stream_is_would_block(result.unwrap_err()))
            return task::Poll<io::Result<usize>>::Ready(rstd::move(result));
        stream.m_registration.clear_readiness(*event);
    }
}

auto SocketStream::poll_flush(mut_ref<SocketStream>, task::Context&)
    -> task::Poll<io::Result<empty>> {
    return task::Poll<io::Result<empty>>::Ready(Ok(empty {}));
}

auto SocketStream::poll_shutdown(mut_ref<SocketStream> self, task::Context&)
    -> task::Poll<io::Result<empty>> {
    return task::Poll<io::Result<empty>>::Ready(self->shutdown());
}

} // namespace rstd::net

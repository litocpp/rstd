module;
#include <rstd/macro.hpp>
export module rstd:net.unix;
export import :net.socket_stream;
#if RSTD_OS_UNIX
export namespace rstd::net
{
class UnixStream {
    SocketStream stream_;
    explicit UnixStream(SocketStream stream): stream_(rstd::move(stream)) {}

public:
    UnixStream(UnixStream&&) noexcept                           = default;
    auto        operator=(UnixStream&&) noexcept -> UnixStream& = default;
    static auto connect(bytes::Bytes name, bool abstract = false)
        -> async::coro<io::Result<UnixStream>>;
    static auto from_owned_socket(os::socket::OwnedSocket socket) -> io::Result<UnixStream>;
    static auto from_owned_fd(os::fd::OwnedFd fd) -> io::Result<UnixStream> {
        return from_owned_socket(
            os::socket::OwnedSocket::from_raw_socket(rstd::move(fd).into_raw_fd()));
    }
    auto into_owned_fd() && { return rstd::move(stream_).into_owned_fd(); }
    auto ready(async::Interest interest) { return stream_.ready(interest); }
    auto readable() { return stream_.readable(); }
    auto writable() { return stream_.writable(); }
    auto try_read(bytes::BytesMut& buf) { return stream_.try_read(buf); }
    auto try_write(const bytes::Bytes& buf) { return stream_.try_write(buf); }
    auto poll_read(mut_ref<UnixStream> self, task::Context& cx, bytes::BytesMut& buf) {
        return stream_.poll_read(mut_ref<SocketStream>::from_raw_parts(&self->stream_), cx, buf);
    }
    auto poll_write(mut_ref<UnixStream> self, task::Context& cx, const bytes::Bytes& buf) {
        return stream_.poll_write(mut_ref<SocketStream>::from_raw_parts(&self->stream_), cx, buf);
    }
    auto poll_flush(mut_ref<UnixStream> self, task::Context& cx) {
        return stream_.poll_flush(mut_ref<SocketStream>::from_raw_parts(&self->stream_), cx);
    }
    auto poll_shutdown(mut_ref<UnixStream> self, task::Context& cx) {
        return stream_.poll_shutdown(mut_ref<SocketStream>::from_raw_parts(&self->stream_), cx);
    }
    auto into_owned_socket() && { return rstd::move(stream_).into_owned_socket(); }
    auto close() && { return rstd::move(stream_).close(); }
    auto recv(usize maximum_size = usize(65536)) const { return stream_.recv(maximum_size); }
    auto send(bytes::Bytes data) const { return stream_.send(rstd::move(data)); }
    auto send_all(bytes::Bytes data) const { return stream_.send_all(rstd::move(data)); }
    auto shutdown() { return stream_.shutdown(); }
    auto shutdown_both() { return stream_.shutdown_both(); }
};
static_assert(Impled<UnixStream, async::io::AsyncRead>);
static_assert(Impled<UnixStream, async::io::AsyncWrite>);
} // namespace rstd::net
#endif

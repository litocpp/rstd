module;
#include <rstd/macro.hpp>

export module rstd:net.socket_stream;
export import :async;
export import :bytes;
export import rstd.io;
import rstd.os.fd;
import rstd.os.socket;

using namespace rstd::prelude;

namespace rstd::net
{

export class TcpStream;
export class TcpConnect;
export class UnixStream;

export class SocketStream {
    friend class TcpStream;
    async::Registration        m_registration;
    async::CompletionSource    m_completion_source;
    Option<async::IoOperation> m_read_operation {};
    Option<async::IoOperation> m_write_operation {};

    friend class TcpConnect;
    friend class UnixStream;

    SocketStream(os::socket::OwnedSocket socket, async::Registration registration)
        : m_registration(rstd::move(registration)),
          m_completion_source(async::CompletionSource::socket(rstd::move(socket))) {}

public:
    SocketStream(const SocketStream&)                        = delete;
    auto operator=(const SocketStream&) -> SocketStream&     = delete;
    SocketStream(SocketStream&&) noexcept                    = default;
    auto operator=(SocketStream&&) noexcept -> SocketStream& = default;

    static auto from_owned_socket(os::socket::OwnedSocket socket) -> io::Result<SocketStream>;
    auto        into_owned_socket() && -> async::coro<io::Result<os::socket::OwnedSocket>>;
    auto        close() && -> async::coro<io::Result<empty>>;
    /// Operations own their socket and buffers before the first poll. Serialize
    /// reads and writes separately; one read and one write may run concurrently.
    auto recv(usize maximum_size = usize(65536)) const -> async::coro<io::Result<bytes::Bytes>>;
    auto send(bytes::Bytes data) const -> async::coro<io::Result<usize>>;
    auto send_all(bytes::Bytes data) const -> async::coro<io::Result<usize>>;
#if RSTD_OS_UNIX
    static auto from_owned_fd(os::fd::OwnedFd fd) -> io::Result<SocketStream>;
    auto        into_owned_fd() && -> async::coro<io::Result<os::fd::OwnedFd>>;
#endif
    auto take_error() -> io::Result<Option<io::Error>>;
    auto shutdown() -> io::Result<empty>;
    auto shutdown_both() -> io::Result<empty>;

    auto ready(async::Interest interest) -> async::ReadinessFuture {
        return async::ReadinessFuture { m_registration, interest };
    }

    auto readable() -> async::ReadinessFuture { return ready(async::Interest::readable()); }
    auto writable() -> async::ReadinessFuture { return ready(async::Interest::writable()); }

    auto try_read(bytes::BytesMut& buf) -> io::Result<usize>;
    auto try_write(bytes::Bytes const& buf) -> io::Result<usize>;

    auto poll_read(mut_ref<SocketStream> self, task::Context& cx, bytes::BytesMut& buf)
        -> task::Poll<io::Result<usize>>;
    auto poll_write(mut_ref<SocketStream> self, task::Context& cx, bytes::Bytes const& buf)
        -> task::Poll<io::Result<usize>>;
    auto poll_flush(mut_ref<SocketStream>, task::Context&) -> task::Poll<io::Result<empty>>;
    auto poll_shutdown(mut_ref<SocketStream> self, task::Context&) -> task::Poll<io::Result<empty>>;
};

static_assert(Impled<SocketStream, async::io::AsyncRead>);
static_assert(Impled<SocketStream, async::io::AsyncWrite>);
} // namespace rstd::net

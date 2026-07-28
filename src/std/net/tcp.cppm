module;
#include <rstd/macro.hpp>

export module rstd:net.tcp;
export import :async;
export import :bytes;
export import :io;
import :net.socket_addr;
import :os.fd;
import :os.socket;

using namespace rstd::prelude;

namespace rstd::net
{

export class TcpStream {
    os::socket::OwnedSocket    m_socket;
    async::Registration        m_registration;
    async::CompletionSource    m_completion_source;
    Option<async::IoOperation> m_read_operation {};
    Option<async::IoOperation> m_write_operation {};

    friend class TcpListener;

    TcpStream(os::socket::OwnedSocket socket, async::Registration registration)
        : m_socket(rstd::move(socket)),
          m_registration(rstd::move(registration)),
          m_completion_source(async::CompletionSource::socket(m_socket.as_socket())) {}

public:
    TcpStream(const TcpStream&)                        = delete;
    auto operator=(const TcpStream&) -> TcpStream&     = delete;
    TcpStream(TcpStream&&) noexcept                    = default;
    auto operator=(TcpStream&&) noexcept -> TcpStream& = default;

    static auto connect(SocketAddr addr) -> async::coro<io::Result<TcpStream>>;
    static auto from_owned_socket(os::socket::OwnedSocket socket) -> io::Result<TcpStream>;
    auto        into_owned_socket() && -> async::coro<io::Result<os::socket::OwnedSocket>>;
#if RSTD_OS_UNIX
    static auto from_owned_fd(os::fd::OwnedFd fd) -> io::Result<TcpStream>;
    auto        into_owned_fd() && -> async::coro<io::Result<os::fd::OwnedFd>>;
#endif
    auto local_addr() const -> io::Result<SocketAddr>;
    auto peer_addr() const -> io::Result<SocketAddr>;
    auto take_error() -> io::Result<Option<io::Error>>;
    auto set_nodelay(bool enabled) -> io::Result<empty>;
    auto shutdown() -> io::Result<empty>;

    auto ready(async::Interest interest) -> async::ReadinessFuture {
        return async::ReadinessFuture { m_registration, interest };
    }

    auto readable() -> async::ReadinessFuture { return ready(async::Interest::readable()); }
    auto writable() -> async::ReadinessFuture { return ready(async::Interest::writable()); }

    auto try_read(bytes::BytesMut& buf) -> io::Result<usize>;
    auto try_write(bytes::Bytes const& buf) -> io::Result<usize>;

    auto poll_read(mut_ref<TcpStream> self, task::Context& cx, bytes::BytesMut& buf)
        -> task::Poll<io::Result<usize>>;
    auto poll_write(mut_ref<TcpStream> self, task::Context& cx, bytes::Bytes const& buf)
        -> task::Poll<io::Result<usize>>;
    auto poll_flush(mut_ref<TcpStream>, task::Context&) -> task::Poll<io::Result<empty>>;
    auto poll_shutdown(mut_ref<TcpStream> self, task::Context&) -> task::Poll<io::Result<empty>>;
};

export class TcpListener {
    os::socket::OwnedSocket m_socket;
    async::Registration     m_registration;
    async::CompletionSource m_completion_source;

    TcpListener(os::socket::OwnedSocket socket, async::Registration registration)
        : m_socket(rstd::move(socket)),
          m_registration(rstd::move(registration)),
          m_completion_source(async::CompletionSource::socket(m_socket.as_socket())) {}

public:
    TcpListener(const TcpListener&)                        = delete;
    auto operator=(const TcpListener&) -> TcpListener&     = delete;
    TcpListener(TcpListener&&) noexcept                    = default;
    auto operator=(TcpListener&&) noexcept -> TcpListener& = default;

    static auto bind(SocketAddr addr) -> io::Result<TcpListener>;
    static auto from_owned_socket(os::socket::OwnedSocket socket) -> io::Result<TcpListener>;
    auto        into_owned_socket() && -> async::coro<io::Result<os::socket::OwnedSocket>>;
#if RSTD_OS_UNIX
    static auto from_owned_fd(os::fd::OwnedFd fd) -> io::Result<TcpListener>;
    auto        into_owned_fd() && -> async::coro<io::Result<os::fd::OwnedFd>>;
#endif
    auto local_addr() const -> io::Result<SocketAddr>;

    auto ready(async::Interest interest) -> async::ReadinessFuture {
        return async::ReadinessFuture { m_registration, interest };
    }

    auto readable() -> async::ReadinessFuture { return ready(async::Interest::readable()); }
    auto try_accept() -> io::Result<tuple<TcpStream, SocketAddr>>;
    auto accept() -> async::coro<io::Result<tuple<TcpStream, SocketAddr>>>;
};

static_assert(Impled<TcpStream, async::io::AsyncRead>);
static_assert(Impled<TcpStream, async::io::AsyncWrite>);

} // namespace rstd::net

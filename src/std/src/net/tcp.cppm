module;
#include <rstd/macro.hpp>

export module rstd:net.tcp;
export import :net.socket_stream;
export import :async;
export import :bytes;
export import rstd.io;
import rstd.net.socket_addr;
import rstd.os.fd;
import rstd.os.socket;

using namespace rstd::prelude;

namespace rstd::net
{

export class TcpConnect;
struct TcpConnectState;

export class TcpStream {
    SocketStream stream_;
    friend class TcpConnect;
    explicit TcpStream(SocketStream stream): stream_(rstd::move(stream)) {}

public:
    TcpStream(TcpStream&&) noexcept                           = default;
    auto        operator=(TcpStream&&) noexcept -> TcpStream& = default;
    static auto connect(SocketAddr addr) -> async::coro<io::Result<TcpStream>>;
    static auto prepare_connect(SocketAddr addr) -> io::Result<TcpConnect>;
    static auto from_owned_socket(os::socket::OwnedSocket socket) -> io::Result<TcpStream>;
    auto        into_owned_socket() && { return rstd::move(stream_).into_owned_socket(); }
    auto        close() && { return rstd::move(stream_).close(); }
    auto        recv(usize maximum_size = usize(65536)) const { return stream_.recv(maximum_size); }
    auto        send(bytes::Bytes data) const { return stream_.send(rstd::move(data)); }
    auto        send_all(bytes::Bytes data) const { return stream_.send_all(rstd::move(data)); }
#if RSTD_OS_UNIX
    static auto from_owned_fd(os::fd::OwnedFd fd) -> io::Result<TcpStream>;
    auto        into_owned_fd() && { return rstd::move(stream_).into_owned_fd(); }
#endif
    auto local_addr() const -> io::Result<SocketAddr>;
    auto peer_addr() const -> io::Result<SocketAddr>;
    auto take_error() { return stream_.take_error(); }
    auto set_nodelay(bool enabled) -> io::Result<empty>;
    auto shutdown() { return stream_.shutdown(); }
    auto ready(async::Interest interest) { return stream_.ready(interest); }
    auto readable() { return stream_.readable(); }
    auto writable() { return stream_.writable(); }
    auto try_read(bytes::BytesMut& buf) { return stream_.try_read(buf); }
    auto try_write(const bytes::Bytes& buf) { return stream_.try_write(buf); }
    auto poll_read(mut_ref<TcpStream> self, task::Context& cx, bytes::BytesMut& buf) {
        return stream_.poll_read(mut_ref<SocketStream>::from_raw_parts(&self->stream_), cx, buf);
    }
    auto poll_write(mut_ref<TcpStream> self, task::Context& cx, const bytes::Bytes& buf) {
        return stream_.poll_write(mut_ref<SocketStream>::from_raw_parts(&self->stream_), cx, buf);
    }
    auto poll_flush(mut_ref<TcpStream> self, task::Context& cx) {
        return stream_.poll_flush(mut_ref<SocketStream>::from_raw_parts(&self->stream_), cx);
    }
    auto poll_shutdown(mut_ref<TcpStream> self, task::Context& cx) {
        return stream_.poll_shutdown(mut_ref<SocketStream>::from_raw_parts(&self->stream_), cx);
    }
};

export class TcpConnect {
    Option<TcpStream>          stream_;
    SocketAddr                 target_;
    sync::Arc<TcpConnectState> state_;

    TcpConnect(TcpStream stream, SocketAddr target);
    friend class TcpStream;

public:
    TcpConnect(const TcpConnect&)                    = delete;
    auto operator=(const TcpConnect&) -> TcpConnect& = delete;
    TcpConnect(TcpConnect&&) noexcept;
    auto operator=(TcpConnect&&) noexcept -> TcpConnect&;
    ~TcpConnect();

    /// Owns the prepared operation before polling. Only one finish is allowed.
    auto finish() const -> async::coro<io::Result<empty>>;
    auto into_stream() && -> io::Result<TcpStream>;
    /// Cancel or drop finish before awaiting close to drain its native operation.
    auto close() && -> async::coro<io::Result<empty>>;
    auto local_addr() const -> io::Result<SocketAddr>;
};

export class TcpListener {
    async::Registration     m_registration;
    async::CompletionSource m_completion_source;

    TcpListener(os::socket::OwnedSocket socket, async::Registration registration)
        : m_registration(rstd::move(registration)),
          m_completion_source(async::CompletionSource::socket(rstd::move(socket))) {}

public:
    TcpListener(const TcpListener&)                        = delete;
    auto operator=(const TcpListener&) -> TcpListener&     = delete;
    TcpListener(TcpListener&&) noexcept                    = default;
    auto operator=(TcpListener&&) noexcept -> TcpListener& = default;

    static auto bind(SocketAddr addr) -> io::Result<TcpListener>;
    static auto from_owned_socket(os::socket::OwnedSocket socket) -> io::Result<TcpListener>;
    auto        into_owned_socket() && -> async::coro<io::Result<os::socket::OwnedSocket>>;
    auto        close() && -> async::coro<io::Result<empty>>;
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

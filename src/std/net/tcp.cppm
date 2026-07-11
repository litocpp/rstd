export module rstd:net.tcp;
export import :async;
export import :bytes;
export import :io;
export import :sys.socket;

namespace rstd::net
{

export using SocketAddr = sys::socket::SocketAddr;
using sys::socket::Socket;

namespace detail
{

inline auto is_error_kind(io::Error const& error, io::ErrorKind::Entity kind) noexcept -> bool {
    return error.kind() == io::ErrorKind { kind };
}

inline auto is_would_block(io::Error const& error) noexcept -> bool {
    return is_error_kind(error, io::ErrorKind::WouldBlock);
}

inline auto is_in_progress(io::Error const& error) noexcept -> bool {
    return is_error_kind(error, io::ErrorKind::InProgress);
}

} // namespace detail

export class TcpStream {
    Socket              m_socket;
    async::Registration m_registration;
    usize               m_read_waiter_id {};
    usize               m_write_waiter_id {};

    friend class TcpListener;

    TcpStream(Socket socket, async::Registration registration)
        : m_socket(rstd::move(socket)), m_registration(rstd::move(registration)) {}

    static auto from_socket(Socket socket) -> io::Result<TcpStream> {
        auto nonblocking = socket.set_nonblocking(true);
        if (nonblocking.is_err()) return Err(rstd::move(nonblocking).unwrap_err_unchecked());

        auto registration = async::Registration::register_fd(socket.as_raw_fd());
        if (registration.is_err()) return Err(rstd::move(registration).unwrap_err_unchecked());
        return Ok(TcpStream { rstd::move(socket), rstd::move(registration).unwrap_unchecked() });
    }

public:
    TcpStream(const TcpStream&)                        = delete;
    auto operator=(const TcpStream&) -> TcpStream&     = delete;
    TcpStream(TcpStream&&) noexcept                    = default;
    auto operator=(TcpStream&&) noexcept -> TcpStream& = default;

    static auto connect(SocketAddr addr) -> async::coro<io::Result<TcpStream>> {
        auto socket_result = Socket::tcp(addr);
        if (socket_result.is_err()) {
            co_return Err(rstd::move(socket_result).unwrap_err_unchecked());
        }

        auto socket         = rstd::move(socket_result).unwrap_unchecked();
        bool connecting     = false;
        auto connect_result = socket.connect(addr);
        if (connect_result.is_err()) {
            auto error = rstd::move(connect_result).unwrap_err_unchecked();
            if (! detail::is_in_progress(error)) {
                co_return Err(rstd::move(error));
            }
            connecting = true;
        }

        auto stream_result = TcpStream::from_socket(rstd::move(socket));
        if (stream_result.is_err()) {
            co_return Err(rstd::move(stream_result).unwrap_err_unchecked());
        }

        auto stream = rstd::move(stream_result).unwrap_unchecked();
        if (connecting) {
            auto ready = co_await stream.writable();
            if (ready.is_err()) co_return Err(rstd::move(ready).unwrap_err_unchecked());

            auto socket_error = stream.m_socket.take_error();
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

    static auto from_owned_fd(sys::fd::OwnedFd fd) -> io::Result<TcpStream> {
        return from_socket(Socket::from_owned_fd(rstd::move(fd)));
    }

    auto into_owned_fd() noexcept -> sys::fd::OwnedFd { return m_socket.into_owned_fd(); }

    auto local_addr() const -> io::Result<SocketAddr> { return m_socket.local_addr(); }
    auto peer_addr() const -> io::Result<SocketAddr> { return m_socket.peer_addr(); }
    auto take_error() -> io::Result<Option<io::Error>> { return m_socket.take_error(); }
    auto set_nodelay(bool enabled) -> io::Result<empty> { return m_socket.set_nodelay(enabled); }
    auto shutdown() -> io::Result<empty> { return m_socket.shutdown_write(); }

    auto ready(async::Interest interest) -> async::ReadinessFuture {
        return async::ReadinessFuture { m_registration, interest };
    }

    auto readable() -> async::ReadinessFuture { return ready(async::Interest::readable()); }

    auto writable() -> async::ReadinessFuture { return ready(async::Interest::writable()); }

    auto try_read(bytes::BytesMut& buf) -> io::Result<usize> {
        auto chunk  = buf.chunk_mut();
        auto result = m_socket.recv(chunk.as_raw_ptr(), chunk.len());
        if (result.is_ok()) {
            auto n = rstd::move(result).unwrap_unchecked();
            buf.advance_mut(n);
            return Ok(n);
        }

        auto error = rstd::move(result).unwrap_err_unchecked();
        if (detail::is_would_block(error)) {
            m_registration.clear_readiness(async::Ready::readable());
        }
        return Err(rstd::move(error));
    }

    auto try_write(bytes::Bytes const& buf) -> io::Result<usize> {
        auto result = m_socket.send(buf.data(), buf.len());
        if (result.is_ok()) return result;

        auto error = rstd::move(result).unwrap_err_unchecked();
        if (detail::is_would_block(error)) {
            m_registration.clear_readiness(async::Ready::writable());
        }
        return Err(rstd::move(error));
    }

    auto poll_read(mut_ref<TcpStream> self, task::Context& cx, bytes::BytesMut& buf)
        -> task::Poll<io::Result<usize>> {
        auto& stream = *self;
        auto  event  = Option<async::ReadyEvent> {};
        while (true) {
            auto chunk  = buf.chunk_mut();
            auto result = stream.m_socket.recv(chunk.as_raw_ptr(), chunk.len());
            if (result.is_ok()) {
                auto n = rstd::move(result).unwrap_unchecked();
                buf.advance_mut(n);
                return task::Poll<io::Result<usize>>::Ready(Ok(n));
            }

            auto error = rstd::move(result).unwrap_err_unchecked();
            if (! detail::is_would_block(error)) {
                return task::Poll<io::Result<usize>>::Ready(Err(rstd::move(error)));
            }

            if (event.is_some()) {
                auto previous = event.take();
                stream.m_registration.clear_readiness(rstd::move(previous).unwrap_unchecked());
            }

            auto ready = stream.m_registration.poll_readiness(
                cx, async::Interest::readable(), stream.m_read_waiter_id);
            if (ready.is_pending()) return task::Poll<io::Result<usize>>::Pending();

            stream.m_read_waiter_id = 0;
            auto ready_result       = rstd::move(ready).take();
            if (ready_result.is_err()) {
                return task::Poll<io::Result<usize>>::Ready(
                    Err(rstd::move(ready_result).unwrap_err_unchecked()));
            }
            event.insert(rstd::move(ready_result).unwrap_unchecked());
        }
    }

    auto poll_write(mut_ref<TcpStream> self, task::Context& cx, bytes::Bytes const& buf)
        -> task::Poll<io::Result<usize>> {
        auto& stream = *self;
        auto  event  = Option<async::ReadyEvent> {};
        while (true) {
            auto result = stream.m_socket.send(buf.data(), buf.len());
            if (result.is_ok()) {
                return task::Poll<io::Result<usize>>::Ready(rstd::move(result));
            }

            auto error = rstd::move(result).unwrap_err_unchecked();
            if (! detail::is_would_block(error)) {
                return task::Poll<io::Result<usize>>::Ready(Err(rstd::move(error)));
            }

            if (event.is_some()) {
                auto previous = event.take();
                stream.m_registration.clear_readiness(rstd::move(previous).unwrap_unchecked());
            }

            auto ready = stream.m_registration.poll_readiness(
                cx, async::Interest::writable(), stream.m_write_waiter_id);
            if (ready.is_pending()) return task::Poll<io::Result<usize>>::Pending();

            stream.m_write_waiter_id = 0;
            auto ready_result        = rstd::move(ready).take();
            if (ready_result.is_err()) {
                return task::Poll<io::Result<usize>>::Ready(
                    Err(rstd::move(ready_result).unwrap_err_unchecked()));
            }
            event.insert(rstd::move(ready_result).unwrap_unchecked());
        }
    }

    auto poll_flush(mut_ref<TcpStream>, task::Context&) -> task::Poll<io::Result<empty>> {
        return task::Poll<io::Result<empty>>::Ready(Ok(empty {}));
    }

    auto poll_shutdown(mut_ref<TcpStream> self, task::Context&) -> task::Poll<io::Result<empty>> {
        auto& stream = *self;
        return task::Poll<io::Result<empty>>::Ready(stream.shutdown());
    }
};

export class TcpListener {
    Socket              m_socket;
    async::Registration m_registration;

    TcpListener(Socket socket, async::Registration registration)
        : m_socket(rstd::move(socket)), m_registration(rstd::move(registration)) {}

public:
    TcpListener(const TcpListener&)                        = delete;
    auto operator=(const TcpListener&) -> TcpListener&     = delete;
    TcpListener(TcpListener&&) noexcept                    = default;
    auto operator=(TcpListener&&) noexcept -> TcpListener& = default;

    static auto bind(SocketAddr addr) -> io::Result<TcpListener> {
        auto socket = Socket::tcp(addr);
        if (socket.is_err()) return Err(rstd::move(socket).unwrap_err_unchecked());

        auto raw   = rstd::move(socket).unwrap_unchecked();
        auto reuse = raw.set_reuseaddr(true);
        if (reuse.is_err()) return Err(rstd::move(reuse).unwrap_err_unchecked());

        auto bound = raw.bind(addr);
        if (bound.is_err()) return Err(rstd::move(bound).unwrap_err_unchecked());

        auto listening = raw.listen();
        if (listening.is_err()) return Err(rstd::move(listening).unwrap_err_unchecked());

        auto registration = async::Registration::register_fd(raw.as_raw_fd());
        if (registration.is_err()) return Err(rstd::move(registration).unwrap_err_unchecked());

        return Ok(TcpListener { rstd::move(raw), rstd::move(registration).unwrap_unchecked() });
    }

    auto local_addr() const -> io::Result<SocketAddr> { return m_socket.local_addr(); }

    auto ready(async::Interest interest) -> async::ReadinessFuture {
        return async::ReadinessFuture { m_registration, interest };
    }

    auto readable() -> async::ReadinessFuture { return ready(async::Interest::readable()); }

    auto try_accept() -> io::Result<tuple<TcpStream, SocketAddr>> {
        auto accepted = m_socket.accept();
        if (accepted.is_err()) {
            auto error = rstd::move(accepted).unwrap_err_unchecked();
            if (detail::is_would_block(error)) {
                m_registration.clear_readiness(async::Ready::readable());
            }
            return Err(rstd::move(error));
        }

        auto accepted_tuple = rstd::move(accepted).unwrap_unchecked();
        auto socket         = rstd::move(accepted_tuple.template get<0>());
        auto addr           = rstd::move(accepted_tuple.template get<1>());

        auto stream = TcpStream::from_socket(rstd::move(socket));
        if (stream.is_err()) return Err(rstd::move(stream).unwrap_err_unchecked());

        return Ok(tuple<TcpStream, SocketAddr> {
            rstd::move(stream).unwrap_unchecked(),
            rstd::move(addr),
        });
    }

    auto accept() -> async::coro<io::Result<tuple<TcpStream, SocketAddr>>> {
        auto event = Option<async::ReadyEvent> {};
        while (true) {
            auto accepted = m_socket.accept();
            if (accepted.is_ok()) {
                auto accepted_tuple = rstd::move(accepted).unwrap_unchecked();
                auto socket         = rstd::move(accepted_tuple.template get<0>());
                auto addr           = rstd::move(accepted_tuple.template get<1>());

                auto stream = TcpStream::from_socket(rstd::move(socket));
                if (stream.is_err()) co_return Err(rstd::move(stream).unwrap_err_unchecked());

                co_return Ok(tuple<TcpStream, SocketAddr> {
                    rstd::move(stream).unwrap_unchecked(),
                    rstd::move(addr),
                });
            }

            auto error = rstd::move(accepted).unwrap_err_unchecked();
            if (! detail::is_would_block(error)) {
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
};

static_assert(Impled<TcpStream, async::io::AsyncRead>);
static_assert(Impled<TcpStream, async::io::AsyncWrite>);

} // namespace rstd::net

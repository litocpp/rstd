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
    auto        into_owned_socket() && { return rstd::move(stream_).into_owned_socket(); }
    auto        close() && { return rstd::move(stream_).close(); }
    auto        recv(usize maximum_size = usize(65536)) const { return stream_.recv(maximum_size); }
    auto        send(bytes::Bytes data) const { return stream_.send(rstd::move(data)); }
    auto        send_all(bytes::Bytes data) const { return stream_.send_all(rstd::move(data)); }
    auto        shutdown() { return stream_.shutdown(); }
    auto        shutdown_both() { return stream_.shutdown_both(); }
};
} // namespace rstd::net
#endif

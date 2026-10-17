module rstd.sys.socket;

namespace rstd::sys::socket
{
auto set_nonblocking(RawSocket socket, bool enabled) -> Result<empty> {
    return platform::set_nonblocking(socket, enabled);
}

auto tcp(SocketAddr const& addr) -> Result<OwnedSocket> {
    return platform::tcp(addr);
}

auto prepare_accept(SocketAddr const& addr) -> Result<Option<OwnedSocket>> {
    return platform::prepare_accept(addr);
}

auto accept_address_buffer_size() noexcept -> usize {
    return platform::accept_address_buffer_size();
}

auto set_reuseaddr(RawSocket socket, bool enabled) -> Result<empty> {
    return platform::set_reuseaddr(socket, enabled);
}

auto set_nodelay(RawSocket socket, bool enabled) -> Result<empty> {
    return platform::set_nodelay(socket, enabled);
}

auto bind(RawSocket socket, SocketAddr const& addr) -> Result<empty> {
    return platform::bind(socket, addr);
}

auto listen(RawSocket socket, i32 backlog) -> Result<empty> {
    return platform::listen(socket, backlog);
}

auto connect(RawSocket socket, SocketAddr const& addr) -> Result<empty> {
    return platform::connect(socket, addr);
}

auto accept(RawSocket socket) -> Result<tuple<OwnedSocket, SocketAddr>> {
    return platform::accept(socket);
}

auto recv(RawSocket socket, mut_ref<byte[]> buf) -> Result<usize> {
    return platform::recv(socket, buf);
}

auto send(RawSocket socket, slice<byte> buf) -> Result<usize> {
    return platform::send(socket, buf);
}

auto shutdown_write(RawSocket socket) -> Result<empty> {
    return platform::shutdown_write(socket);
}

auto take_error(RawSocket socket) -> Result<Option<Error>> {
    return platform::take_error(socket);
}

auto local_addr(RawSocket socket) -> Result<SocketAddr> {
    return platform::local_addr(socket);
}

auto peer_addr(RawSocket socket) -> Result<SocketAddr> {
    return platform::peer_addr(socket);
}
} // namespace rstd::sys::socket

module;
#include <rstd/macro.hpp>
module rstd.sys.socket;

namespace rstd::sys::socket
{
auto set_nonblocking(RawSocket socket, bool enabled) -> IOResult<empty> {
    return platform::set_nonblocking(socket, enabled);
}

auto tcp(SocketAddr const& addr) -> IOResult<OwnedSocket> {
    return platform::tcp(addr);
}

#if RSTD_OS_UNIX
auto unix_stream() -> IOResult<OwnedSocket> {
    return platform::unix_stream();
}
auto connect_unix(RawSocket socket, slice<byte> name, bool abstract) -> IOResult<empty> {
    return platform::connect_unix(socket, name, abstract);
}
#endif

auto prepare_accept(SocketAddr const& addr) -> IOResult<Option<OwnedSocket>> {
    return platform::prepare_accept(addr);
}

auto accept_address_buffer_size() noexcept -> usize {
    return platform::accept_address_buffer_size();
}

auto set_reuseaddr(RawSocket socket, bool enabled) -> IOResult<empty> {
    return platform::set_reuseaddr(socket, enabled);
}

auto set_nodelay(RawSocket socket, bool enabled) -> IOResult<empty> {
    return platform::set_nodelay(socket, enabled);
}

auto bind(RawSocket socket, SocketAddr const& addr) -> IOResult<empty> {
    return platform::bind(socket, addr);
}

auto listen(RawSocket socket, i32 backlog) -> IOResult<empty> {
    return platform::listen(socket, backlog);
}

auto connect(RawSocket socket, SocketAddr const& addr) -> IOResult<empty> {
    return platform::connect(socket, addr);
}

auto accept(RawSocket socket) -> IOResult<tuple<OwnedSocket, SocketAddr>> {
    return platform::accept(socket);
}

auto recv(RawSocket socket, mut_ref<byte[]> buf) -> IOResult<usize> {
    return platform::recv(socket, buf);
}

auto send(RawSocket socket, slice<byte> buf) -> IOResult<usize> {
    return platform::send(socket, buf);
}

auto shutdown_write(RawSocket socket) -> IOResult<empty> {
    return platform::shutdown_write(socket);
}

auto shutdown_both(RawSocket socket) -> IOResult<empty> {
    return platform::shutdown_both(socket);
}

auto take_error(RawSocket socket) -> IOResult<Option<Error>> {
    return platform::take_error(socket);
}

auto local_addr(RawSocket socket) -> IOResult<SocketAddr> {
    return platform::local_addr(socket);
}

auto peer_addr(RawSocket socket) -> IOResult<SocketAddr> {
    return platform::peer_addr(socket);
}
} // namespace rstd::sys::socket

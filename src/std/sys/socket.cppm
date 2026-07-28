module;
#include <rstd/macro.hpp>

export module rstd:sys.socket;
export import :io.error;
export import :net.socket_addr;
export import :os.socket;
#if RSTD_OS_UNIX
import :sys.pal.unix.socket;
#elif RSTD_OS_WINDOWS
import :sys.pal.windows.socket;
#endif
import rstd.core;

namespace rstd::sys::socket
{

using rstd::io::Result;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::net::SocketAddr;
using rstd::os::socket::OwnedSocket;
using rstd::os::socket::RawSocket;

#if RSTD_OS_UNIX
namespace platform = rstd::sys::pal::unix::socket;
#elif RSTD_OS_WINDOWS
namespace platform = rstd::sys::pal::windows::socket;
#endif

export auto set_nonblocking(RawSocket socket, bool enabled) -> Result<empty> {
    return platform::set_nonblocking(socket, enabled);
}

export auto tcp(SocketAddr const& addr) -> Result<OwnedSocket> {
    return platform::tcp(addr);
}

export auto prepare_accept(SocketAddr const& addr) -> Result<Option<OwnedSocket>> {
    return platform::prepare_accept(addr);
}

export auto accept_address_buffer_size() noexcept -> usize {
    return platform::accept_address_buffer_size();
}

export auto set_reuseaddr(RawSocket socket, bool enabled) -> Result<empty> {
    return platform::set_reuseaddr(socket, enabled);
}

export auto set_nodelay(RawSocket socket, bool enabled) -> Result<empty> {
    return platform::set_nodelay(socket, enabled);
}

export auto bind(RawSocket socket, SocketAddr const& addr) -> Result<empty> {
    return platform::bind(socket, addr);
}

export auto listen(RawSocket socket, i32 backlog = i32(128)) -> Result<empty> {
    return platform::listen(socket, backlog);
}

export auto connect(RawSocket socket, SocketAddr const& addr) -> Result<empty> {
    return platform::connect(socket, addr);
}

export auto accept(RawSocket socket) -> Result<tuple<OwnedSocket, SocketAddr>> {
    return platform::accept(socket);
}

export auto recv(RawSocket socket, mut_ref<byte[]> buf) -> Result<usize> {
    return platform::recv(socket, buf);
}

export auto send(RawSocket socket, slice<byte> buf) -> Result<usize> {
    return platform::send(socket, buf);
}

export auto shutdown_write(RawSocket socket) -> Result<empty> {
    return platform::shutdown_write(socket);
}

export auto take_error(RawSocket socket) -> Result<Option<Error>> {
    return platform::take_error(socket);
}

export auto local_addr(RawSocket socket) -> Result<SocketAddr> {
    return platform::local_addr(socket);
}

export auto peer_addr(RawSocket socket) -> Result<SocketAddr> {
    return platform::peer_addr(socket);
}

} // namespace rstd::sys::socket

export module rstd.sys.socket:unix;
export import rstd.io.error;
export import rstd.net.socket_addr;
export import rstd.os.socket;
import rstd.sys.libc;
import rstd.sys.pal.poll.types;
import rstd.core;

template<typename T>
using IOResult = rstd::io::Result<T>;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::net::Ipv4Addr;
using rstd::net::Ipv6Addr;
using rstd::net::SocketAddr;
using rstd::os::socket::OwnedSocket;
using rstd::os::socket::RawSocket;
namespace libc = rstd::sys::libc;

namespace rstd::sys::pal::unix::socket
{

export struct NativeSocketAddr {
    libc::sockaddr_storage storage {};
    libc::socklen_t        len {};
};

export auto addr_to_native(const rstd::sys::pal::poll::SocketAddress& address) noexcept
    -> NativeSocketAddr;

export auto addr_to_native(SocketAddr const& address) noexcept -> NativeSocketAddr;
export auto addr_from_native(const libc::sockaddr* address, libc::socklen_t len)
    -> IOResult<SocketAddr>;

export auto set_nonblocking(RawSocket socket, bool enabled) -> IOResult<empty>;
export auto set_cloexec(RawSocket socket) -> IOResult<empty>;

export auto tcp(SocketAddr const& addr) -> IOResult<OwnedSocket>;
export auto unix_stream() -> IOResult<OwnedSocket>;
export auto connect_unix(RawSocket socket, slice<byte> name, bool abstract) -> IOResult<empty>;

export auto prepare_accept(SocketAddr const&) -> IOResult<Option<OwnedSocket>>;

export auto accept_address_buffer_size() noexcept -> usize;

export auto set_reuseaddr(RawSocket socket, bool enabled) -> IOResult<empty>;

export auto set_nodelay(RawSocket socket, bool enabled) -> IOResult<empty>;
export auto set_keepalive(RawSocket socket, bool enabled, Option<u32> idle, Option<u32> interval)
    -> IOResult<empty>;
export auto keepalive(RawSocket socket) -> IOResult<bool>;
export auto keepalive_timing_supported() -> bool;

export auto bind(RawSocket socket, SocketAddr const& addr) -> IOResult<empty>;

export auto listen(RawSocket socket, i32 backlog) -> IOResult<empty>;

export auto connect(RawSocket socket, SocketAddr const& addr) -> IOResult<empty>;

export auto accept(RawSocket socket) -> IOResult<tuple<OwnedSocket, SocketAddr>>;

export auto recv(RawSocket socket, mut_ref<byte[]> buf) -> IOResult<usize>;

export auto send(RawSocket socket, slice<byte> buf) -> IOResult<usize>;

export auto shutdown_write(RawSocket socket) -> IOResult<empty>;
export auto shutdown_both(RawSocket socket) -> IOResult<empty>;

export auto take_error(RawSocket socket) -> IOResult<Option<Error>>;

export auto local_addr(RawSocket socket) -> IOResult<SocketAddr>;

export auto peer_addr(RawSocket socket) -> IOResult<SocketAddr>;
} // namespace rstd::sys::pal::unix::socket

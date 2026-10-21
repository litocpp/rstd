module;
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <limits.h>
#pragma comment(lib, "ws2_32.lib")

export module rstd.sys.socket:windows;
export import rstd.io.error;
export import rstd.net.socket_addr;
export import rstd.os.socket;
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
using rstd::sys::pal::poll::SocketAddress;

namespace rstd::sys::pal::windows::socket
{

export struct NativeSocketAddr {
    sockaddr_storage storage {};
    int              len {};
};
export auto ensure_started() -> IOResult<empty>;
export auto addr_to_native(SocketAddr const& address) noexcept -> NativeSocketAddr;
export auto addr_from_native(const sockaddr* address, int len) -> IOResult<SocketAddr>;

export inline constexpr rstd::size_t ACCEPT_ADDRESS_BUFFER_SIZE =
    2 * (sizeof(sockaddr_storage) + 16);

export auto accept_address_buffer_size() noexcept -> usize;

export auto start_connect(RawSocket socket, SocketAddress const& address, void* overlapped)
    -> IOResult<empty>;

export auto finish_connect(RawSocket socket) -> IOResult<empty>;

export auto start_accept(RawSocket listener,
                         RawSocket accepted,
                         void*     address_buffer,
                         usize     address_buffer_len,
                         void*     overlapped) -> IOResult<empty>;

export auto finish_accept(RawSocket listener, RawSocket accepted) -> IOResult<empty>;

export auto set_nonblocking(RawSocket socket, bool enabled) -> IOResult<empty>;

export auto tcp(SocketAddr const& addr) -> IOResult<OwnedSocket>;

export auto prepare_accept(SocketAddr const& addr) -> IOResult<Option<OwnedSocket>>;

export auto set_reuseaddr(RawSocket socket, bool enabled) -> IOResult<empty>;

export auto set_nodelay(RawSocket socket, bool enabled) -> IOResult<empty>;

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
} // namespace rstd::sys::pal::windows::socket

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

namespace rstd::sys::pal::windows::socket
{

using rstd::io::Result;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::net::Ipv4Addr;
using rstd::net::Ipv6Addr;
using rstd::net::SocketAddr;
using rstd::os::socket::OwnedSocket;
using rstd::os::socket::RawSocket;
using rstd::sys::pal::poll::SocketAddress;

export inline constexpr rstd::size_t ACCEPT_ADDRESS_BUFFER_SIZE =
    2 * (sizeof(sockaddr_storage) + 16);

export auto accept_address_buffer_size() noexcept -> usize;

export auto start_connect(RawSocket socket, SocketAddress const& address, void* overlapped)
    -> Result<empty>;

export auto finish_connect(RawSocket socket) -> Result<empty>;

export auto start_accept(RawSocket listener,
                         RawSocket accepted,
                         void*     address_buffer,
                         usize     address_buffer_len,
                         void*     overlapped) -> Result<empty>;

export auto finish_accept(RawSocket listener, RawSocket accepted) -> Result<empty>;

export auto set_nonblocking(RawSocket socket, bool enabled) -> Result<empty>;

export auto tcp(SocketAddr const& addr) -> Result<OwnedSocket>;

export auto prepare_accept(SocketAddr const& addr) -> Result<Option<OwnedSocket>>;

export auto set_reuseaddr(RawSocket socket, bool enabled) -> Result<empty>;

export auto set_nodelay(RawSocket socket, bool enabled) -> Result<empty>;

export auto bind(RawSocket socket, SocketAddr const& addr) -> Result<empty>;

export auto listen(RawSocket socket, i32 backlog) -> Result<empty>;

export auto connect(RawSocket socket, SocketAddr const& addr) -> Result<empty>;

export auto accept(RawSocket socket) -> Result<tuple<OwnedSocket, SocketAddr>>;

export auto recv(RawSocket socket, mut_ref<byte[]> buf) -> Result<usize>;

export auto send(RawSocket socket, slice<byte> buf) -> Result<usize>;

export auto shutdown_write(RawSocket socket) -> Result<empty>;

export auto take_error(RawSocket socket) -> Result<Option<Error>>;

export auto local_addr(RawSocket socket) -> Result<SocketAddr>;

export auto peer_addr(RawSocket socket) -> Result<SocketAddr>;
} // namespace rstd::sys::pal::windows::socket

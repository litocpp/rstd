module;
#include <rstd/macro.hpp>

export module rstd.sys.socket;
export import rstd.io.error;
export import rstd.net.socket_addr;
export import rstd.os.socket;
export import :datagram;
#if RSTD_OS_UNIX
export import :unix;
#elif RSTD_OS_WINDOWS
export import :windows;
#endif
import rstd.core;
import rstd.alloc;
import rstd.net.lookup_host;

template<typename T>
using IOResult = rstd::io::Result<T>;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::net::SocketAddr;
using rstd::os::socket::OwnedSocket;
using rstd::os::socket::RawSocket;

namespace rstd::sys::socket
{

#if RSTD_OS_UNIX
namespace platform = rstd::sys::pal::unix::socket;
#elif RSTD_OS_WINDOWS
namespace platform = rstd::sys::pal::windows::socket;
#endif

export auto lookup_host(ref<ffi::CStr> host, u16 port)
    -> Result<::alloc::vec::Vec<net::SocketAddr>, net::LookupError>;

export auto set_nonblocking(RawSocket socket, bool enabled) -> IOResult<empty>;

export auto tcp(SocketAddr const& addr) -> IOResult<OwnedSocket>;
#if RSTD_OS_UNIX
export auto unix_stream() -> IOResult<OwnedSocket>;
export auto connect_unix(RawSocket socket, slice<byte> name, bool abstract) -> IOResult<empty>;
#endif

export auto prepare_accept(SocketAddr const& addr) -> IOResult<Option<OwnedSocket>>;

export auto accept_address_buffer_size() noexcept -> usize;

export auto set_reuseaddr(RawSocket socket, bool enabled) -> IOResult<empty>;

export auto set_nodelay(RawSocket socket, bool enabled) -> IOResult<empty>;
export auto set_keepalive(RawSocket socket, bool enabled, Option<u32> idle, Option<u32> interval)
    -> IOResult<empty>;
export auto keepalive(RawSocket socket) -> IOResult<bool>;
export auto keepalive_timing_supported() -> bool;

export auto bind(RawSocket socket, SocketAddr const& addr) -> IOResult<empty>;

export auto listen(RawSocket socket, i32 backlog = i32(128)) -> IOResult<empty>;

export auto connect(RawSocket socket, SocketAddr const& addr) -> IOResult<empty>;

export auto accept(RawSocket socket) -> IOResult<tuple<OwnedSocket, SocketAddr>>;

export auto recv(RawSocket socket, mut_ref<byte[]> buf) -> IOResult<usize>;

export auto send(RawSocket socket, slice<byte> buf) -> IOResult<usize>;

export auto shutdown_write(RawSocket socket) -> IOResult<empty>;
export auto shutdown_both(RawSocket socket) -> IOResult<empty>;

export auto take_error(RawSocket socket) -> IOResult<Option<Error>>;

export auto local_addr(RawSocket socket) -> IOResult<SocketAddr>;

export auto peer_addr(RawSocket socket) -> IOResult<SocketAddr>;

} // namespace rstd::sys::socket

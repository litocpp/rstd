module;
#include <rstd/macro.hpp>
#if RSTD_OS_LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#endif

module rstd.os.linux.net;
using namespace rstd::prelude;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;

#if RSTD_OS_LINUX
auto linux_socket_family(rstd::os::socket::BorrowedSocket socket) -> rstd::io::Result<int> {
    sockaddr_storage address {};
    socklen_t        size = sizeof(address);
    if (::getsockname(socket.as_raw_socket(), reinterpret_cast<sockaddr*>(&address), &size) != 0)
        return Err(Error::last_os_error());
    if (size < sizeof(sa_family_t) ||
        (address.ss_family != AF_INET && address.ss_family != AF_INET6))
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    return Ok(int(address.ss_family));
}
auto linux_socket_option(rstd::os::socket::BorrowedSocket socket,
                         int                              level,
                         int                              option,
                         const void*                      value,
                         socklen_t                        size) -> rstd::io::Result<empty> {
    if (::setsockopt(socket.as_raw_socket(), level, option, value, size) != 0)
        return Err(Error::last_os_error());
    return Ok(empty {});
}
#endif

namespace rstd::os::linux::net
{
auto set_mark(socket::BorrowedSocket socket, u32 value) -> io::Result<empty> {
#if RSTD_OS_LINUX
    auto mark = value.to_primitive();
    return linux_socket_option(socket, SOL_SOCKET, SO_MARK, &mark, sizeof(mark));
#else
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}
auto set_transparent(socket::BorrowedSocket socket, bool enabled) -> io::Result<empty> {
#if RSTD_OS_LINUX
    auto family = rstd_try(linux_socket_family(socket));
    int  value  = enabled ? 1 : 0;
    return linux_socket_option(socket,
                               family == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
                               family == AF_INET ? IP_TRANSPARENT : IPV6_TRANSPARENT,
                               &value,
                               sizeof(value));
#else
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}
auto set_recv_original_destination(socket::BorrowedSocket socket, bool enabled)
    -> io::Result<empty> {
#if RSTD_OS_LINUX
    auto family = rstd_try(linux_socket_family(socket));
    int  value  = enabled ? 1 : 0;
    return linux_socket_option(socket,
                               family == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
                               family == AF_INET ? IP_RECVORIGDSTADDR : IPV6_RECVORIGDSTADDR,
                               &value,
                               sizeof(value));
#else
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
}
} // namespace rstd::os::linux::net

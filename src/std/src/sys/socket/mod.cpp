module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <errno.h>
#endif
module rstd.sys.socket;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;
using rstd::ffi::CStr;
using rstd::net::LookupError;

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

auto set_keepalive(RawSocket socket, bool enabled, Option<u32> idle, Option<u32> interval)
    -> IOResult<empty> {
    return platform::set_keepalive(socket, enabled, idle, interval);
}
auto keepalive(RawSocket socket) -> IOResult<bool> {
    return platform::keepalive(socket);
}

auto keepalive_timing_supported() -> bool {
    return platform::keepalive_timing_supported();
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

namespace rstd::sys::socket
{
struct LookupAddresses {
    addrinfo* list {};
    LookupAddresses()                                          = default;
    LookupAddresses(const LookupAddresses&)                    = delete;
    auto operator=(const LookupAddresses&) -> LookupAddresses& = delete;
    ~LookupAddresses() {
        if (list) ::freeaddrinfo(list);
    }
};

auto lookup_host(ref<CStr> host, u16 port) -> Result<Vec<SocketAddr>, LookupError> {
#if RSTD_OS_WINDOWS
    auto started = platform::ensure_started();
    if (started.is_err())
        return Err(LookupError {
            i32(), String::make("Winsock initialization failed"_str), Some(started.unwrap_err()) });
#endif
    addrinfo hints {};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* list    = nullptr;
    auto      code    = ::getaddrinfo(host.as_ptr(), nullptr, &hints, &list);
    if (code != 0) {
        Option<Error> cause;
#if RSTD_OS_WINDOWS
        auto text = ::gai_strerrorA(code);
#else
        if (code == EAI_SYSTEM) cause = Some(Error::from_raw_os_error(i32(errno)));
        auto text = ::gai_strerror(code);
#endif
        auto message = text ? CStr::from_ptr(text).to_str().unwrap_or("name lookup failed"_str)
                            : "name lookup failed"_str;
        return Err(LookupError { i32(code), String::make(message), cause });
    }
    LookupAddresses addresses;
    addresses.list = list;
    Vec<SocketAddr> result;
    for (auto p = addresses.list; p; p = p->ai_next) {
        auto address = platform::addr_from_native(p->ai_addr, p->ai_addrlen);
        if (address.is_err()) continue;
        auto value     = address->with_port(port);
        bool duplicate = false;
        for (const auto& previous : result) {
            if (previous == value) {
                duplicate = true;
                break;
            }
        }
        if (! duplicate) result.push(rstd::move(value));
    }
    if (result.is_empty())
        return Err(LookupError { i32(), String::make("no stream addresses"_str), {} });
    return Ok(rstd::move(result));
}
} // namespace rstd::sys::socket

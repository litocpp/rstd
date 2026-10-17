export module rstd.tests.std_socket_addr_module;
import rstd.core;
import rstd.net.socket_addr;

using namespace rstd;
using namespace rstd::literals;
using net::SocketAddr;

constexpr auto loopback = SocketAddr::ipv4_loopback(u16(80));
static_assert(loopback.is_ipv4() && loopback.port() == u16(80));

export auto std_socket_addr_module() -> bool {
    auto v4      = SocketAddr::parse("127.0.0.1:80"_str);
    auto v6      = from_str<SocketAddr>("[::1%12]:443"_str);
    auto invalid = SocketAddr::parse("[::1]:65536"_str);
    if (invalid.is_ok()) return false;
    auto error = invalid.unwrap_err();
    return v4.is_ok() && *v4 == loopback && v6.is_ok() &&
           *v6 == SocketAddr::ipv6(net::Ipv6Addr::loopback(), u16(443), u32(), u32(12)) &&
           as<error::Error>(error).source().is_none();
}

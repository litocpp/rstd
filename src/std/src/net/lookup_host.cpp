module rstd.net.lookup_host;
import rstd.sys.socket;

using namespace rstd::prelude;
using namespace rstd::literals;
using ::alloc::ffi::CString;
using ::alloc::string::String;
using ::alloc::vec::Vec;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;

auto rstd::net::lookup_host(ref<str> host, u16 port) -> Result<Vec<SocketAddr>, LookupError> {
    auto invalid = [](ref<str> message) {
        return Err(LookupError { i32(),
                                 String::make(message),
                                 Some(Error::from_kind(ErrorKind { ErrorKind::InvalidInput })) });
    };
    if (host.is_empty()) return invalid("empty host"_str);
    if (auto address = Ipv4Addr::parse(host); address.is_ok()) {
        Vec<SocketAddr> result;
        result.push(SocketAddr::ipv4(*address, port));
        return Ok(rstd::move(result));
    }
    if (auto address = Ipv6Addr::parse(host); address.is_ok()) {
        Vec<SocketAddr> result;
        result.push(SocketAddr::ipv6(*address, port));
        return Ok(rstd::move(result));
    }
    auto name = CString::make(Vec<u8>::from(host.as_bytes()));
    if (name.is_err()) return invalid("NUL in host"_str);
    return sys::socket::lookup_host(name->as_ref(), port);
}

export module rstd.tests.std_sys_socket_module;
import rstd.sys.socket;

export auto std_sys_socket_module() -> bool {
    using namespace rstd;
    namespace socket = rstd::sys::socket;
    auto address     = net::SocketAddr::ipv4_loopback(u16());
    auto listener    = socket::tcp(address);
    if (listener.is_err()) return false;
    auto raw = listener->as_raw_socket();
    if (socket::set_reuseaddr(raw, true).is_err() || socket::bind(raw, address).is_err() ||
        socket::listen(raw).is_err())
        return false;
    auto bound = socket::local_addr(raw);
    if (bound.is_err() || bound->port() == u16()) return false;
    auto pending = socket::accept(raw);
    if (pending.is_ok() ||
        pending.unwrap_err().kind() != io::error::ErrorKind { io::error::ErrorKind::WouldBlock })
        return false;
    auto client = socket::tcp(*bound);
    if (client.is_err()) return false;
    if (socket::set_nonblocking(client->as_raw_socket(), false).is_err() ||
        socket::connect(client->as_raw_socket(), *bound).is_err())
        return false;
    if (socket::set_nonblocking(raw, false).is_err()) return false;
    auto accepted = socket::accept(raw);
    if (accepted.is_err()) return false;
    auto peer = socket::peer_addr(client->as_raw_socket());
    if (peer.is_err() || *peer != *bound) return false;
    auto error = socket::take_error(client->as_raw_socket());
    return error.is_ok() && error->is_none();
}

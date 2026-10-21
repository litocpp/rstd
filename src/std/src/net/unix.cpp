module;
#include <rstd/macro.hpp>
module rstd;
import :net.unix;
import rstd.sys.socket;
#if RSTD_OS_UNIX
using namespace rstd::prelude;
namespace rstd::net
{
auto UnixStream::from_owned_socket(os::socket::OwnedSocket socket) -> io::Result<UnixStream> {
    auto stream = SocketStream::from_owned_socket(rstd::move(socket));
    if (stream.is_err()) return Err(rstd::move(stream).unwrap_err());
    return Ok(UnixStream(rstd::move(stream).unwrap()));
}
auto UnixStream::connect(bytes::Bytes name, bool abstract) -> async::coro<io::Result<UnixStream>> {
    auto owned = sys::socket::unix_stream();
    if (owned.is_err()) co_return Err(rstd::move(owned).unwrap_err());
    auto raw    = owned->as_raw_socket();
    auto stream = from_owned_socket(rstd::move(owned).unwrap());
    if (stream.is_err()) co_return Err(rstd::move(stream).unwrap_err());
    auto result = sys::socket::connect_unix(raw, as_bytes(name.as_slice()), abstract);
    if (result.is_err()) {
        auto error = rstd::move(result).unwrap_err();
        if (error.kind() != io::ErrorKind { io::ErrorKind::InProgress }) co_return Err(error);
        auto ready = co_await stream->stream_.writable();
        if (ready.is_err()) co_return Err(rstd::move(ready).unwrap_err());
        auto failure = stream->stream_.take_error();
        if (failure.is_err()) co_return Err(rstd::move(failure).unwrap_err());
        if (failure->is_some()) co_return Err(**failure);
    }
    co_return Ok(rstd::move(stream).unwrap());
}
} // namespace rstd::net
#endif

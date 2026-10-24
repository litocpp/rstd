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
enum class UnixConnectPhase
{
    Fresh,
    Running,
    Connected,
    Failed,
    Closing,
    Consumed
};
struct UnixConnectState {
    sync::Mutex<UnixConnectPhase> phase { UnixConnectPhase::Fresh };
    Option<UnixStream>            stream;
    bytes::Bytes                  name;
    bool                          abstract;
    UnixConnectState(UnixStream stream, bytes::Bytes name, bool abstract)
        : stream(Some(rstd::move(stream))), name(rstd::move(name)), abstract(abstract) {}
};
struct UnixConnectWork {
    sync::Arc<UnixConnectState> state;
    async::CompletionSource     source;
    async::ReadinessFuture      ready;
    UnixConnectWork(sync::Arc<UnixConnectState> state,
                    async::CompletionSource     source,
                    async::ReadinessFuture      ready)
        : state(rstd::move(state)), source(rstd::move(source)), ready(rstd::move(ready)) {}
    UnixConnectWork(UnixConnectWork&&) noexcept = default;
    ~UnixConnectWork() {
        if (state) {
            auto phase = state->phase.lock().unwrap();
            if (*phase == UnixConnectPhase::Running) *phase = UnixConnectPhase::Failed;
        }
    }
};

auto unix_connect_failure(io::ErrorKind::Entity kind) -> async::coro<io::Result<empty>> {
    co_return Err(io::Error::from_kind(io::ErrorKind { kind }));
}
auto unix_finish_connect(UnixConnectWork work) -> async::coro<io::Result<empty>> {
    bool pending;
    {
        auto phase = work.state->phase.lock().unwrap();
        if (*phase != UnixConnectPhase::Running)
            co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        auto connected = sys::socket::connect_unix(
            work.source.as_raw_fd(), as_bytes(work.state->name.as_slice()), work.state->abstract);
        pending = connected.is_err();
        if (pending && connected.unwrap_err().kind() != io::ErrorKind { io::ErrorKind::InProgress })
            co_return Err(connected.unwrap_err());
    }
    if (pending) {
        auto ready = co_await rstd::move(work.ready);
        if (ready.is_err()) co_return Err(ready.unwrap_err());
    }
    auto phase = work.state->phase.lock().unwrap();
    if (*phase != UnixConnectPhase::Running)
        co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
    if (pending) {
        auto failure = sys::socket::take_error(work.source.as_raw_fd());
        if (failure.is_err()) co_return Err(failure.unwrap_err());
        if (failure->is_some()) co_return Err(**failure);
    }
    *phase = UnixConnectPhase::Connected;
    co_return Ok(empty {});
}

auto UnixStream::prepare_connect(bytes::Bytes name, bool abstract) -> io::Result<UnixConnect> {
    auto owned = sys::socket::unix_stream();
    if (owned.is_err()) return Err(rstd::move(owned).unwrap_err());
    auto stream = from_owned_socket(rstd::move(owned).unwrap());
    if (stream.is_err()) return Err(rstd::move(stream).unwrap_err());
    return Ok(UnixConnect(rstd::move(stream).unwrap(), rstd::move(name), abstract));
}
UnixConnect::UnixConnect(UnixStream stream, bytes::Bytes name, bool abstract)
    : state_(sync::Arc<UnixConnectState>::make(rstd::move(stream), rstd::move(name), abstract)) {
}
UnixConnect::UnixConnect(UnixConnect&&) noexcept                    = default;
auto UnixConnect::operator=(UnixConnect&&) noexcept -> UnixConnect& = default;
UnixConnect::~UnixConnect()                                         = default;

auto UnixConnect::finish() const -> async::coro<io::Result<empty>> {
    if (! state_) return unix_connect_failure(io::ErrorKind::InvalidInput);
    auto phase = state_->phase.lock().unwrap();
    if (*phase != UnixConnectPhase::Fresh || state_->stream.is_none())
        return unix_connect_failure(io::ErrorKind::InvalidInput);
    *phase       = UnixConnectPhase::Running;
    auto& stream = state_->stream->stream_;
    return unix_finish_connect(
        UnixConnectWork(state_.clone(), stream.m_completion_source.clone(), stream.writable()));
}
auto UnixConnect::into_stream() && -> io::Result<UnixStream> {
    if (! state_) return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
    auto phase = state_->phase.lock().unwrap();
    if (*phase != UnixConnectPhase::Connected || state_->stream.is_none())
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
    *phase = UnixConnectPhase::Consumed;
    return Ok(state_->stream.take().unwrap());
}
auto UnixConnect::close() && -> async::coro<io::Result<empty>> {
    if (! state_) return unix_connect_failure(io::ErrorKind::NotConnected);
    Option<UnixStream> stream;
    {
        auto phase = state_->phase.lock().unwrap();
        if (state_->stream.is_none()) return unix_connect_failure(io::ErrorKind::NotConnected);
        *phase = UnixConnectPhase::Closing;
        stream = state_->stream.take();
    }
    return rstd::move(stream.unwrap()).close();
}
auto UnixStream::connect(bytes::Bytes name, bool abstract) -> async::coro<io::Result<UnixStream>> {
    auto prepared = prepare_connect(rstd::move(name), abstract);
    if (prepared.is_err()) co_return Err(prepared.unwrap_err());
    auto connecting = rstd::move(prepared).unwrap();
    auto connected  = co_await connecting.finish();
    if (connected.is_err()) {
        auto error = connected.unwrap_err();
        (void)co_await rstd::move(connecting).close();
        co_return Err(error);
    }
    co_return rstd::move(connecting).into_stream();
}
} // namespace rstd::net
#endif

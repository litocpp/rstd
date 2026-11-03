module rstd;
import :async.stream;

using namespace rstd::prelude;
namespace rstd::async::io
{
using Backend = sync::Arc<dyn<CompletionStream>>;
auto closed_stream() -> ::rstd::io::Error {
    return ::rstd::io::Error::from_kind(
        ::rstd::io::ErrorKind { ::rstd::io::ErrorKind::NotConnected });
}
auto receive_owned(Backend backend, bytes::BytesMut buffer) -> coro<StreamReceive> {
    if (! backend) co_return StreamReceive { Err(closed_stream()), rstd::move(buffer) };
    co_return co_await backend->recv_into(rstd::move(buffer));
}
auto receive_bytes(Backend backend, usize maximum, alloc::SharedAllocator allocator)
    -> coro<::rstd::io::Result<bytes::Bytes>> {
    if (! backend) co_return Err(closed_stream());
    if (maximum == usize())
        co_return Err(::rstd::io::Error::from_kind(
            ::rstd::io::ErrorKind { ::rstd::io::ErrorKind::InvalidInput }));
    auto allocated = bytes::BytesMut::try_with_capacity_in(maximum, rstd::move(allocator));
    if (allocated.is_err())
        co_return Err(::rstd::io::Error::from_kind(
            ::rstd::io::ErrorKind { ::rstd::io::ErrorKind::OutOfMemory }));
    auto received = co_await backend->recv_into(rstd::move(allocated).unwrap());
    if (received.result.is_err()) co_return Err(rstd::move(received.result).unwrap_err());
    if (*received.result != received.buffer.len())
        co_return Err(::rstd::io::Error::from_kind(
            ::rstd::io::ErrorKind { ::rstd::io::ErrorKind::InvalidData }));
    co_return Ok(received.buffer.freeze());
}
auto send_owned(Backend backend, bytes::Bytes data, bool all) -> coro<::rstd::io::Result<usize>> {
    if (! backend) co_return Err(closed_stream());
    if (! all) co_return co_await backend->send(rstd::move(data));
    auto length = data.len();
    while (! data.is_empty()) {
        auto sent = co_await backend->send(data.clone());
        if (sent.is_err()) co_return Err(rstd::move(sent).unwrap_err());
        if (*sent == usize() || *sent > data.len())
            co_return Err(::rstd::io::Error::from_kind(
                ::rstd::io::ErrorKind { ::rstd::io::ErrorKind::WriteZero }));
        data.advance(*sent);
    }
    co_return Ok(length);
}
enum class StreamAction
{
    Flush,
    ShutdownWrite,
    WaitClosed,
    Close
};
auto stream_action(Backend backend, StreamAction action) -> coro<::rstd::io::Result<empty>> {
    if (! backend) co_return Err(closed_stream());
    if (action == StreamAction::Flush) co_return co_await backend->flush();
    if (action == StreamAction::ShutdownWrite) co_return co_await backend->shutdown_write();
    auto waited = co_await backend->wait_closed();
    if (waited.is_err()) co_return Err(rstd::move(waited).unwrap_err());
    if (action == StreamAction::WaitClosed) co_return Ok(empty {});
    auto result = backend->take_close_result();
    if (! result)
        co_return Err(::rstd::io::Error::from_kind(
            ::rstd::io::ErrorKind { ::rstd::io::ErrorKind::InvalidInput }));
    co_return result.take().unwrap();
}
Stream::Stream(Backend backend): backend_(rstd::move(backend)) {
}
Stream::Stream(Stream&&) noexcept = default;
auto Stream::operator=(Stream&& other) noexcept -> Stream& {
    if (this != &other) {
        request_stop();
        backend_ = rstd::move(other.backend_);
    }
    return *this;
}
Stream::~Stream() {
    request_stop();
}
auto Stream::recv_into(bytes::BytesMut buffer) const -> coro<StreamReceive> {
    return receive_owned(backend_.clone(), rstd::move(buffer));
}
auto Stream::recv(usize maximum, alloc::SharedAllocator allocator) const
    -> coro<::rstd::io::Result<bytes::Bytes>> {
    return receive_bytes(backend_.clone(), maximum, rstd::move(allocator));
}
auto Stream::send(bytes::Bytes data) const -> coro<::rstd::io::Result<usize>> {
    return send_owned(backend_.clone(), rstd::move(data), false);
}
auto Stream::send_all(bytes::Bytes data) const -> coro<::rstd::io::Result<usize>> {
    return send_owned(backend_.clone(), rstd::move(data), true);
}
auto Stream::flush() const -> coro<::rstd::io::Result<empty>> {
    return stream_action(backend_.clone(), StreamAction::Flush);
}
auto Stream::shutdown_write() const -> coro<::rstd::io::Result<empty>> {
    return stream_action(backend_.clone(), StreamAction::ShutdownWrite);
}
void Stream::request_stop() const {
    if (backend_) backend_->request_stop();
}
auto Stream::close() const -> coro<::rstd::io::Result<empty>> {
    return stream_action(backend_.clone(), StreamAction::Close);
}
auto Stream::wait_closed() const -> coro<::rstd::io::Result<empty>> {
    return stream_action(backend_.clone(), StreamAction::WaitClosed);
}
auto Stream::take_close_result() const -> Option<::rstd::io::Result<empty>> {
    if (! backend_) return None();
    return backend_->take_close_result();
}
} // namespace rstd::async::io

module;
#include <rstd/macro.hpp>
export module rstd:async.pipe;
export import :async.coro_future;
export import :async.reactor;
export import :bytes;
export import rstd.os.fd;
import rstd.sys.io.stdio;
import rstd.sys.libc;
import :sync;
#if RSTD_OS_WINDOWS
import :async.io_operation;
export import rstd.os.handle;
#endif

using namespace rstd::prelude;
namespace libc = rstd::sys::libc;

namespace rstd::async::pipe
{
#if RSTD_OS_WINDOWS
struct PipeReadFields {
    Option<IoOperation>  operation;
    Option<bytes::Bytes> buffered;
    usize                next_waiter { 1 };
    Option<usize>        waiter;
};
class PipeRead;
struct PipeState {
    CompletionSource            source;
    sync::Mutex<PipeReadFields> reads { PipeReadFields {} };
    sync::atomic::Atomic<bool>  closing { false };
    explicit PipeState(CompletionSource source): source(rstd::move(source)) {}
    static auto closed() -> io::Error {
        return io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected });
    }
    static auto make(os::handle::OwnedHandle handle, bool writing)
        -> io::Result<sync::Arc<PipeState>> {
        if (! handle.is_open())
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
        auto valid = sys::io::stdio::validate_async_pipe(handle.as_handle(), writing);
        if (valid.is_err()) return Err(rstd::move(valid).unwrap_err());
        return Ok(sync::Arc<PipeState>::make(CompletionSource::file(rstd::move(handle))));
    }
    static auto make(os::fd::OwnedFd descriptor, bool writing) -> io::Result<sync::Arc<PipeState>> {
        if (! descriptor.is_open())
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
        return make(os::handle::OwnedHandle::from_raw_handle(rstd::move(descriptor).into_raw_fd()),
                    writing);
    }
    static auto read_owned(sync::Arc<PipeState> state, usize maximum)
        -> coro<io::Result<bytes::Bytes>>;
    static auto write_owned(sync::Arc<PipeState> state, bytes::Bytes bytes, bool all)
        -> coro<io::Result<usize>> {
        usize offset {};
        while (offset < bytes.len()) {
            if (state->closing.load()) co_return Err(closed());
            auto result =
                co_await IoOperation::write(state->source, bytes.slice(offset, bytes.len()));
            if (result.is_err()) co_return Err(rstd::move(result).unwrap_err());
            auto written = result->transferred();
            if (written == usize())
                co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::WriteZero }));
            offset += written;
            if (! all) break;
        }
        co_return Ok(offset);
    }
    static auto close_owned(sync::Arc<PipeState> state) -> coro<io::Result<empty>> {
        state->closing.store(true);
        Option<IoOperation>  operation;
        Option<bytes::Bytes> buffered;
        {
            auto reads = state->reads.lock().unwrap();
            operation  = reads->operation.take();
            buffered   = reads->buffered.take();
        }
        operation = None();
        buffered  = None();
        co_return co_await rstd::move(state->source).release();
    }
};

// The endpoint retains submitted reads across cancellation of a polling waiter.
class PipeRead {
    sync::Arc<PipeState> state_;
    usize                maximum_;
    Option<usize>        waiter_;
    bool                 completed_ {};
    void                 detach() {
        if (! state_ || waiter_.is_none()) return;
        Option<task::Waker> discarded;
        {
            auto reads = state_->reads.lock().unwrap();
            if (reads->waiter == waiter_) {
                reads->waiter = None();
                if (reads->operation.is_some()) discarded = reads->operation->take_poll_waker();
            }
        }
        waiter_ = None();
    }

public:
    using Output = io::Result<bytes::Bytes>;
    PipeRead(sync::Arc<PipeState> state, usize maximum)
        : state_(rstd::move(state)), maximum_(maximum) {}
    PipeRead(const PipeRead&) = delete;
    PipeRead(PipeRead&& other) noexcept
        : state_(rstd::move(other.state_)),
          maximum_(other.maximum_),
          waiter_(other.waiter_.take()),
          completed_(other.completed_) {}
    ~PipeRead() { detach(); }
    auto poll(mut_ref<PipeRead> self, task::Context& cx) -> task::Poll<Output> {
        if (self->completed_) rstd::panic { "pipe read polled after completion" };
        auto finish = [&](Output result) {
            self->completed_ = true;
            return task::Poll<Output>::Ready(rstd::move(result));
        };
        if (self->maximum_ == usize())
            return finish(Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput })));
        if (self->state_->closing.load()) return finish(Err(PipeState::closed()));
        Option<IoOperation>  operation;
        Option<bytes::Bytes> buffered;
        {
            auto reads = self->state_->reads.lock().unwrap();
            if (self->waiter_.is_none()) {
                if (reads->waiter.is_some())
                    return finish(
                        Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::ResourceBusy })));
                self->waiter_ = Some(reads->next_waiter++);
                reads->waiter = self->waiter_;
            }
            buffered = reads->buffered.take();
            if (buffered.is_none()) {
                operation = reads->operation.take();
                if (operation.is_none())
                    operation = Some(IoOperation::read(self->state_->source, self->maximum_));
            }
        }
        if (buffered.is_none()) {
            auto polled = operation->poll(future::as_mut_ref(*operation), cx);
            if (polled.is_pending()) {
                {
                    auto reads = self->state_->reads.lock().unwrap();
                    if (! self->state_->closing.load()) {
                        reads->operation = operation.take();
                        return task::Poll<Output>::Pending();
                    }
                    reads->waiter = None();
                    self->waiter_ = None();
                }
                return finish(Err(PipeState::closed()));
            }
            auto result = rstd::move(polled).take();
            operation   = None();
            if (result.is_err()) {
                self->detach();
                auto error = rstd::move(result).unwrap_err();
                if (error.raw_os_error() == Some(i32(libc::ERROR_BROKEN_PIPE)))
                    return finish(Ok(bytes::Bytes::make()));
                return finish(Err(rstd::move(error)));
            }
            buffered = Some(rstd::move(result).unwrap().into_data());
        }
        auto count = buffered->len();
        if (count > self->maximum_) count = self->maximum_;
        auto data = buffered->split_to(count);
        {
            auto reads = self->state_->reads.lock().unwrap();
            if (! buffered->is_empty()) reads->buffered = buffered.take();
            reads->waiter = None();
            self->waiter_ = None();
        }
        return finish(Ok(rstd::move(data)));
    }
};
inline auto PipeState::read_owned(sync::Arc<PipeState> state, usize maximum)
    -> coro<io::Result<bytes::Bytes>> {
    co_return co_await PipeRead { rstd::move(state), maximum };
}
#else
struct PipeState {
    os::fd::RawFd              fd;
    Registration               registration;
    sync::atomic::Atomic<bool> closing { false };
    PipeState(os::fd::RawFd fd, Registration registration)
        : fd(fd), registration(rstd::move(registration)) {}
    static auto make(os::fd::OwnedFd descriptor, bool writing) -> io::Result<sync::Arc<PipeState>> {
#if RSTD_OS_UNIX
        int          fd = descriptor.as_raw_fd();
        libc::stat_t metadata {};
        if (libc::fstat(fd, &metadata) < 0)
            return Err(io::Error::from_raw_os_error(i32(libc::get_errno())));
        if ((metadata.st_mode & libc::S_IFMT) != libc::S_IFIFO)
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
        int flags = libc::fcntl(fd, libc::F_GETFL);
        if (flags < 0) return Err(io::Error::from_raw_os_error(i32(libc::get_errno())));
        int mode = flags & libc::O_ACCMODE;
        if (mode != libc::O_RDWR && mode != (writing ? libc::O_WRONLY : libc::O_RDONLY))
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
        if (libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK) < 0)
            return Err(io::Error::from_raw_os_error(i32(libc::get_errno())));
        auto registration = Registration::register_owned_fd(rstd::move(descriptor));
        if (registration.is_err()) return Err(rstd::move(registration).unwrap_err());
        return Ok(sync::Arc<PipeState>::make(fd, rstd::move(registration).unwrap()));
#else
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
#endif
    }
    static auto closed() -> io::Error {
        return io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected });
    }
    static auto read_owned(sync::Arc<PipeState> state, usize maximum)
        -> coro<io::Result<bytes::Bytes>> {
#if RSTD_OS_UNIX
        if (maximum == usize())
            co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
        Vec<u8> buffer;
        buffer.resize(maximum, u8());
        Option<ReadyEvent> ready;
        for (;;) {
            if (state->closing.load()) co_return Err(closed());
            auto writable =
                mut_ref<u8[]>::from_raw_parts(buffer.as_mut_slice().as_raw_ptr(), buffer.len());
            auto result = sys::io::stdio::read_fd(state->fd, as_bytes_mut(writable));
            if (result.is_ok()) {
                buffer.truncate(*result);
                co_return Ok(bytes::Bytes::from_vec(rstd::move(buffer)));
            }
            if (result.unwrap_err().kind() != io::ErrorKind { io::ErrorKind::WouldBlock })
                co_return Err(rstd::move(result).unwrap_err());
            if (ready.is_some()) state->registration.clear_readiness(ready.take().unwrap());
            auto event = co_await ReadinessFuture { state->registration, Interest::readable() };
            if (event.is_err()) co_return Err(rstd::move(event).unwrap_err());
            ready = Some(rstd::move(event).unwrap());
        }
#else
        co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
#endif
    }
    static auto write_owned(sync::Arc<PipeState> state, bytes::Bytes bytes, bool all)
        -> coro<io::Result<usize>> {
#if RSTD_OS_UNIX
        usize              offset {};
        Option<ReadyEvent> ready;
        while (offset < bytes.len()) {
            if (state->closing.load()) co_return Err(closed());
            auto remaining = slice<u8>::from_raw_parts(
                bytes.as_slice().as_raw_ptr() + offset.to_primitive(), bytes.len() - offset);
            auto result = sys::io::stdio::write_pipe_fd(state->fd, as_bytes(remaining));
            if (result.is_ok()) {
                if (*result == usize())
                    co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::WriteZero }));
                offset += *result;
                if (! all) co_return Ok(offset);
                continue;
            }
            if (result.unwrap_err().kind() != io::ErrorKind { io::ErrorKind::WouldBlock })
                co_return Err(rstd::move(result).unwrap_err());
            if (ready.is_some()) state->registration.clear_readiness(ready.take().unwrap());
            auto event = co_await ReadinessFuture { state->registration, Interest::writable() };
            if (event.is_err()) co_return Err(rstd::move(event).unwrap_err());
            ready = Some(rstd::move(event).unwrap());
        }
        co_return Ok(offset);
#else
        co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
#endif
    }
    static auto close_owned(sync::Arc<PipeState> state) -> coro<io::Result<empty>> {
        state->closing.store(true);
        co_return co_await rstd::move(state->registration).deregister();
    }
};

#endif

export class Receiver {
    sync::Arc<PipeState> state_;
    explicit Receiver(sync::Arc<PipeState> state): state_(rstd::move(state)) {}

public:
    Receiver(const Receiver&)                        = delete;
    Receiver(Receiver&&) noexcept                    = default;
    auto operator=(Receiver&&) noexcept -> Receiver& = default;
    // Consumes the descriptor on success and failure; accepts only readable pipes/FIFOs.
    static auto from_owned_fd(os::fd::OwnedFd descriptor) -> io::Result<Receiver> {
        auto state = PipeState::make(rstd::move(descriptor), false);
        if (state.is_err()) return Err(rstd::move(state).unwrap_err());
        return Ok(Receiver { rstd::move(state).unwrap() });
    }
#if RSTD_OS_WINDOWS
    static auto from_owned_handle(os::handle::OwnedHandle handle) -> io::Result<Receiver> {
        auto state = PipeState::make(rstd::move(handle), false);
        if (state.is_err()) return Err(rstd::move(state).unwrap_err());
        return Ok(Receiver { rstd::move(state).unwrap() });
    }
#endif
    // Serialize reads. Canceling a pending read preserves its data for the next read.
    auto read(usize maximum = usize(65536)) const {
        return PipeState::read_owned(state_.clone(), maximum);
    }
    // Finish or drop outstanding operations before awaiting close.
    auto close() && { return PipeState::close_owned(rstd::move(state_)); }
};

export class Sender {
    sync::Arc<PipeState> state_;
    explicit Sender(sync::Arc<PipeState> state): state_(rstd::move(state)) {}

public:
    Sender(const Sender&)                        = delete;
    Sender(Sender&&) noexcept                    = default;
    auto operator=(Sender&&) noexcept -> Sender& = default;
    // Consumes the descriptor on success and failure; accepts only writable pipes/FIFOs.
    static auto from_owned_fd(os::fd::OwnedFd descriptor) -> io::Result<Sender> {
        auto state = PipeState::make(rstd::move(descriptor), true);
        if (state.is_err()) return Err(rstd::move(state).unwrap_err());
        return Ok(Sender { rstd::move(state).unwrap() });
    }
#if RSTD_OS_WINDOWS
    static auto from_owned_handle(os::handle::OwnedHandle handle) -> io::Result<Sender> {
        auto state = PipeState::make(rstd::move(handle), true);
        if (state.is_err()) return Err(rstd::move(state).unwrap_err());
        return Ok(Sender { rstd::move(state).unwrap() });
    }
#endif
    // Serialize writes. Canceled writes can be partial.
    auto write(bytes::Bytes bytes) const {
        return PipeState::write_owned(state_.clone(), rstd::move(bytes), false);
    }
    auto write_all(bytes::Bytes bytes) const {
        return PipeState::write_owned(state_.clone(), rstd::move(bytes), true);
    }
    // Finish or drop outstanding operations before awaiting close.
    auto close() && { return PipeState::close_owned(rstd::move(state_)); }
};
} // namespace rstd::async::pipe

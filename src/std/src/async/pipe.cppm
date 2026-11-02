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

using namespace rstd::prelude;
namespace libc = rstd::sys::libc;

namespace rstd::async::pipe
{
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
    // Serialize reads. A canceled pending read consumes no bytes.
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

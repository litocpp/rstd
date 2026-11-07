module;
#include <rstd/macro.hpp>
#include <sys/event.h>
#include <sys/types.h>
#include <time.h>
#include <cstdint>

export module rstd:sys.pal.macos.poll;
export import rstd.sys.pal.poll.types;
import :sys.pal.unix.readiness;
export import rstd.io.error;

import rstd.sys.libc;
import rstd.alloc;

namespace libc = rstd::sys::libc;

using ::alloc::sync::Arc;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::sys::pal::poll::Batch;
using rstd::sys::pal::poll::BackendPreference;
using rstd::sys::pal::poll::Capabilities;
using rstd::sys::pal::poll::Capability;
using rstd::sys::pal::poll::Event;
using rstd::sys::pal::poll::Ready;
using rstd::sys::pal::poll::WaitMode;
using rstd::sys::pal::unix::readiness::ReadinessState;
using rstd::sys::pal::unix::readiness::SourceState;

namespace rstd::sys::pal::macos::poll
{

inline constexpr rstd::uint64_t WAKE_KEY  = 0;
inline constexpr rstd::uint64_t TIMER_KEY = rstd::uint64_t(-1);

struct WakeState {
    os::fd::OwnedFd fd;

    explicit WakeState(os::fd::OwnedFd fd): fd(rstd::move(fd)) {}
};

struct MacosSource : SourceState {
    bool registered_read { false };
    bool registered_write { false };

    auto is_registered() const noexcept -> bool { return registered_read || registered_write; }

    MacosSource(os::fd::RawFd fd, u32 slot, u32 generation = u32(1))
        : SourceState(fd, slot, generation) {}
};

export class PollWake {
    Arc<WakeState> m_state;

public:
    explicit PollWake(Arc<WakeState> state): m_state(rstd::move(state)) {}

    PollWake(const PollWake&)                        = delete;
    auto operator=(const PollWake&) -> PollWake&     = delete;
    PollWake(PollWake&&) noexcept                    = default;
    auto operator=(PollWake&&) noexcept -> PollWake& = default;

    auto clone() const -> PollWake { return PollWake { m_state.clone() }; }

    auto wake() const -> io::Result<empty> {
        auto value = rstd::uint64_t(1);
        auto rc    = libc::write(m_state->fd.as_raw_fd(), &value, sizeof(value));
        if (rc == static_cast<decltype(rc)>(sizeof(value)) || libc::get_errno() == libc::EAGAIN) {
            return Ok(empty {});
        }
        return Err(Error::last_os_error());
    }
};

export struct PollInit;

export class Poller : private ReadinessState<MacosSource, Poller> {
    friend class ReadinessState<MacosSource, Poller>;
    os::fd::OwnedFd        m_poll_fd;
    os::fd::OwnedFd        m_wake_read;
    Option<time::Duration> m_armed_timer {};

    static auto ready_from_kqueue(const struct kevent& event) noexcept -> Ready {
        auto ready = Ready {};
        switch (event.filter) {
        case EVFILT_READ: ready.bits |= Ready::READABLE; break;
        case EVFILT_WRITE: ready.bits |= Ready::WRITABLE; break;
        default: break;
        }
        if ((event.flags & EV_EOF) != 0) {
            ready.bits |= (event.filter == EVFILT_WRITE ? Ready::WRITE_CLOSED : Ready::READ_CLOSED);
        }
        if ((event.flags & EV_ERROR) != 0) ready.bits |= Ready::ERROR;
        return ready;
    }

    static void drain_counter(os::fd::RawFd fd) noexcept {
        auto value = rstd::uint64_t {};
        (void)libc::read(fd, &value, sizeof(value));
    }

    static auto duration_to_ms(time::Duration duration) noexcept -> rstd::int64_t {
        auto ms = duration.as_millis().to_primitive();
        if (ms <= 0) ms = 1;
        return static_cast<rstd::int64_t>(ms);
    }

    auto arm_timer(Option<time::Duration> timeout) -> io::Result<empty> {
        if (timeout.is_none() && m_armed_timer.is_none()) return Ok(empty {});
        struct kevent change;
        if (timeout.is_some()) {
            EV_SET(&change,
                   static_cast<uintptr_t>(TIMER_KEY),
                   EVFILT_TIMER,
                   EV_ADD | EV_ONESHOT,
                   0,
                   static_cast<intptr_t>(duration_to_ms(*timeout)),
                   reinterpret_cast<void*>(static_cast<uintptr_t>(TIMER_KEY)));
        } else {
            EV_SET(
                &change, static_cast<uintptr_t>(TIMER_KEY), EVFILT_TIMER, EV_DELETE, 0, 0, nullptr);
        }
        if (::kevent(m_poll_fd.as_raw_fd(), &change, 1, nullptr, 0, nullptr) < 0) {
            return Err(Error::last_os_error());
        }
        m_armed_timer = rstd::move(timeout);
        return Ok(empty {});
    }

    auto refresh_source(MacosSource& source) -> io::Result<empty> {
        auto interest   = combined_interest(source);
        auto want_read  = interest.is_readable();
        auto want_write = interest.is_writable();

        struct kevent changes[4];
        int           n            = 0;
        const bool    change_read  = want_read != source.registered_read;
        const bool    change_write = want_write != source.registered_write;

        if (want_read && change_read) {
            EV_SET(&changes[n++],
                   static_cast<uintptr_t>(source.fd),
                   EVFILT_READ,
                   EV_ADD | EV_CLEAR,
                   0,
                   0,
                   reinterpret_cast<void*>(static_cast<uintptr_t>(source.native_key)));
        } else if (! want_read && change_read) {
            EV_SET(&changes[n++],
                   static_cast<uintptr_t>(source.fd),
                   EVFILT_READ,
                   EV_DELETE,
                   0,
                   0,
                   nullptr);
        }

        if (want_write && change_write) {
            EV_SET(&changes[n++],
                   static_cast<uintptr_t>(source.fd),
                   EVFILT_WRITE,
                   EV_ADD | EV_CLEAR,
                   0,
                   0,
                   reinterpret_cast<void*>(static_cast<uintptr_t>(source.native_key)));
        } else if (! want_write && change_write) {
            EV_SET(&changes[n++],
                   static_cast<uintptr_t>(source.fd),
                   EVFILT_WRITE,
                   EV_DELETE,
                   0,
                   0,
                   nullptr);
        }

        if (n > 0 && ::kevent(m_poll_fd.as_raw_fd(), changes, n, nullptr, 0, nullptr) < 0) {
            return Err(Error::last_os_error());
        }
        if (n > 0) {
            if (change_read) source.registered_read = want_read;
            if (change_write) source.registered_write = want_write;
        }
        return Ok(empty {});
    }

public:
    using ReadinessState::register_readiness;
    using ReadinessState::update_readiness;
    using ReadinessState::deregister_readiness;
    using ReadinessState::submit_operation;
    using ReadinessState::release_completion_source;
    using ReadinessState::cancel_operation;
    using ReadinessState::begin_shutdown;
    using ReadinessState::has_pending_operations;

    Poller()                                     = default;
    Poller(const Poller&)                        = delete;
    auto operator=(const Poller&) -> Poller&     = delete;
    Poller(Poller&&) noexcept                    = default;
    auto operator=(Poller&&) noexcept -> Poller& = default;

    Poller(os::fd::OwnedFd poll_fd, os::fd::OwnedFd wake_read)
        : m_poll_fd(rstd::move(poll_fd)),
          m_wake_read(rstd::move(wake_read)),
          m_armed_timer(None()) {}

    static auto init(BackendPreference preference) -> io::Result<PollInit>;

    auto capabilities() const noexcept -> Capabilities {
        return Capabilities::of(Capability::Readiness) | Capability::SocketCompletion |
               Capability::Timer | Capability::Wake;
    }

    auto wait(WaitMode mode, Option<time::Duration> next_timer) -> io::Result<Batch> {
        auto batch = Batch {};
        drain_ready_events(batch);
        if (! batch.is_empty()) return Ok(rstd::move(batch));

        auto armed = arm_timer(next_timer);
        if (armed.is_err()) return Err(rstd::move(armed).unwrap_err_unchecked());

        struct kevent   events[Batch::capacity()];
        struct timespec zero {};
        const auto*     timeout = mode == WaitMode::Immediate ? &zero : nullptr;

        int count {};
        do {
            count = ::kevent(m_poll_fd.as_raw_fd(),
                             nullptr,
                             0,
                             events,
                             static_cast<int>(Batch::capacity()),
                             const_cast<struct timespec*>(timeout));
        } while (count < 0 && libc::get_errno() == libc::EINTR);
        if (count < 0) return Err(Error::last_os_error());

        for (int i = 0; i < count; ++i) {
            auto& event      = events[i];
            auto  native_key = rstd::uint64_t(reinterpret_cast<uintptr_t>(event.udata));

            if (event.filter == EVFILT_TIMER && native_key == TIMER_KEY) {
                m_armed_timer = None<time::Duration>();
                continue;
            }
            if (event.filter == EVFILT_READ && native_key == WAKE_KEY) {
                drain_counter(m_wake_read.as_raw_fd());
                m_ready_events.push(Event::wake());
                continue;
            }

            auto refreshed = dispatch_ready(native_key, ready_from_kqueue(event));
            if (refreshed.is_err()) return Err(rstd::move(refreshed).unwrap_err_unchecked());
        }
        drain_ready_events(batch);
        return Ok(rstd::move(batch));
    }
};

export struct PollInit {
    Poller   poller;
    PollWake wake;

    PollInit(Poller poller, PollWake wake): poller(rstd::move(poller)), wake(rstd::move(wake)) {}
};

inline auto Poller::init(BackendPreference preference) -> io::Result<PollInit> {
    if (preference == BackendPreference::NativeCompletionRequired) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
    }

    auto kq = ::kqueue();
    if (kq < 0) return Err(Error::last_os_error());
    const auto kq_flags = libc::fcntl(kq, libc::F_GETFD, 0);
    if (kq_flags < 0 || libc::fcntl(kq, libc::F_SETFD, kq_flags | libc::FD_CLOEXEC) < 0) {
        auto error = Error::last_os_error();
        libc::close(kq);
        return Err(rstd::move(error));
    }
    auto owned_kq = os::fd::OwnedFd::from_raw_fd(kq);

    int pipe_fds[2];
    if (libc::pipe2(pipe_fds, libc::O_NONBLOCK | libc::O_CLOEXEC) != 0) {
        return Err(Error::last_os_error());
    }
    auto owned_read  = os::fd::OwnedFd::from_raw_fd(pipe_fds[0]);
    auto owned_write = os::fd::OwnedFd::from_raw_fd(pipe_fds[1]);

    // Register the wake pipe's read end on the kqueue.
    struct kevent wake_ev;
    EV_SET(&wake_ev,
           static_cast<uintptr_t>(pipe_fds[0]),
           EVFILT_READ,
           EV_ADD | EV_CLEAR,
           0,
           0,
           reinterpret_cast<void*>(WAKE_KEY));
    if (::kevent(kq, &wake_ev, 1, nullptr, 0, nullptr) < 0) {
        return Err(Error::last_os_error());
    }

    auto wake_state = Arc<WakeState>::make(rstd::move(owned_write));
    return Ok(PollInit {
        Poller { rstd::move(owned_kq), rstd::move(owned_read) },
        PollWake { rstd::move(wake_state) },
    });
}

} // namespace rstd::sys::pal::macos::poll

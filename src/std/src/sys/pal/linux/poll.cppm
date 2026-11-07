export module rstd:sys.pal.linux.poll;
export import rstd.sys.pal.poll.types;
import :sys.pal.unix.readiness;

import rstd.sys.libc;
import :sys.pal.linux.io_uring;
import rstd.alloc;

namespace libc  = rstd::sys::libc;
namespace uring = rstd::sys::pal::linux::io_uring;

using ::alloc::sync::Arc;
using ::alloc::boxed::Box;
using rstd::io::error::Error;
using rstd::sys::pal::poll::Batch;
using rstd::sys::pal::poll::BackendPreference;
using rstd::sys::pal::poll::Capabilities;
using rstd::sys::pal::poll::Capability;
using rstd::sys::pal::poll::Event;
using rstd::sys::pal::poll::Interest;
using rstd::sys::pal::poll::Operation;
using rstd::sys::pal::poll::Ready;
using rstd::sys::pal::poll::WaitMode;
using rstd::sys::pal::unix::readiness::ReadinessState;
using rstd::sys::pal::unix::readiness::SourceState;

namespace rstd::sys::pal::linux::poll
{

inline constexpr rstd::uint64_t WAKE_KEY  = 0;
inline constexpr rstd::uint64_t TIMER_KEY = rstd::uint64_t(-1);
inline constexpr rstd::uint64_t RING_KEY  = TIMER_KEY - 1;

struct WakeState {
    os::fd::OwnedFd fd;

    explicit WakeState(os::fd::OwnedFd fd): fd(rstd::move(fd)) {}
};

struct LinuxSource : SourceState {
    bool registered { false };

    auto is_registered() const noexcept -> bool { return registered; }

    LinuxSource(os::fd::RawFd fd, u32 slot, u32 generation = u32(1))
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

export class Poller : private ReadinessState<LinuxSource, Poller> {
    friend class ReadinessState<LinuxSource, Poller>;
    os::fd::OwnedFd            m_poll_fd;
    os::fd::OwnedFd            m_wake_fd;
    os::fd::OwnedFd            m_timer_fd;
    Option<time::Duration>     m_armed_timer;
    libc::epoll_event          m_events[Batch::capacity()] {};
    Option<Box<uring::Driver>> m_ring {};

    static auto native_interest(Interest interest) noexcept -> rstd::uint32_t {
        rstd::uint32_t events {};
        if (interest.is_readable()) {
            events |= libc::EPOLLIN;
            if (libc::HAS_EPOLLRDHUP) events |= libc::EPOLLRDHUP;
        }
        if (interest.is_writable()) events |= libc::EPOLLOUT;
        return events;
    }

    static auto ready_from_native(rstd::uint32_t events) noexcept -> Ready {
        auto ready = Ready {};
        if ((events & libc::EPOLLIN) != 0) ready.bits |= Ready::READABLE;
        if ((events & libc::EPOLLOUT) != 0) ready.bits |= Ready::WRITABLE;
        if (libc::HAS_EPOLLRDHUP && (events & libc::EPOLLRDHUP) != 0) {
            ready.bits |= Ready::READ_CLOSED;
        }
        if ((events & libc::EPOLLHUP) != 0) {
            ready.bits |= Ready::READ_CLOSED | Ready::WRITE_CLOSED;
        }
        if ((events & libc::EPOLLERR) != 0) ready.bits |= Ready::ERROR;
        return ready;
    }

    static void drain_counter(os::fd::RawFd fd) noexcept {
        auto value = rstd::uint64_t {};
        (void)libc::read(fd, &value, sizeof(value));
    }

    static auto duration_to_timespec(time::Duration duration) noexcept -> libc::timespec {
        return libc::timespec {
            .tv_sec  = static_cast<libc::time_t>(duration.as_secs().to_primitive()),
            .tv_nsec = static_cast<long>(duration.subsec_nanos().to_primitive()),
        };
    }

    auto arm_timer(Option<time::Duration> timeout) -> io::Result<empty> {
        if (timeout.is_none() && m_armed_timer.is_none()) return Ok(empty {});
        auto spec = libc::itimerspec_t {};
        if (timeout.is_some()) {
            auto duration = *timeout;
            if (duration.is_zero()) duration = time::Duration::from_nanos(u64(1));
            spec.it_value = duration_to_timespec(duration);
        }
        if (libc::timerfd_settime(m_timer_fd.as_raw_fd(), 0, &spec, nullptr) < 0) {
            return Err(Error::last_os_error());
        }
        m_armed_timer = rstd::move(timeout);
        return Ok(empty {});
    }

    auto refresh_source(LinuxSource& source) -> io::Result<empty> {
        auto interest = combined_interest(source);
        if (interest.is_empty()) {
            if (source.registered &&
                libc::epoll_ctl(m_poll_fd.as_raw_fd(), libc::EPOLL_CTL_DEL, source.fd, nullptr) <
                    0) {
                auto error = libc::get_errno();
                // Closing the descriptor already removes its epoll entry.
                if (error != libc::EBADF && error != libc::ENOENT)
                    return Err(Error::from_raw_os_error(i32(error)));
            }
            source.registered = false;
            return Ok(empty {});
        }

        auto event = libc::epoll_event {};
        event.events =
            native_interest(interest) | libc::EPOLLERR | libc::EPOLLHUP | libc::EPOLLONESHOT;
        event.data.u64 = source.native_key;
        auto operation = source.registered ? libc::EPOLL_CTL_MOD : libc::EPOLL_CTL_ADD;
        if (libc::epoll_ctl(m_poll_fd.as_raw_fd(), operation, source.fd, &event) < 0) {
            return Err(Error::last_os_error());
        }
        source.registered = true;
        return Ok(empty {});
    }

public:
    using ReadinessState::register_readiness;
    using ReadinessState::update_readiness;
    using ReadinessState::deregister_readiness;

    Poller()                                     = default;
    Poller(const Poller&)                        = delete;
    auto operator=(const Poller&) -> Poller&     = delete;
    Poller(Poller&&) noexcept                    = default;
    auto operator=(Poller&&) noexcept -> Poller& = default;

    Poller(os::fd::OwnedFd            poll_fd,
           os::fd::OwnedFd            wake_fd,
           os::fd::OwnedFd            timer_fd,
           Option<Box<uring::Driver>> ring)
        : m_poll_fd(rstd::move(poll_fd)),
          m_wake_fd(rstd::move(wake_fd)),
          m_timer_fd(rstd::move(timer_fd)),
          m_armed_timer(None()),
          m_ring(rstd::move(ring)) {}

    static auto init(BackendPreference preference) -> io::Result<PollInit>;

    auto capabilities() const noexcept -> Capabilities {
        auto result = Capabilities::of(Capability::Readiness) | Capability::SocketCompletion |
                      Capability::Timer | Capability::Wake;
        if (m_ring.is_some()) result = result | Capability::FileCompletion;
        return result;
    }

    auto submit_operation(Operation operation) -> io::Result<empty> {
        if (m_ring.is_some()) return m_ring->deref_mut()->submit_operation(rstd::move(operation));
        return ReadinessState::submit_operation(rstd::move(operation));
    }
    auto release_completion_source(u64 source_key, os::fd::RawFd fd) -> io::Result<empty> {
        if (m_ring.is_some()) return Ok(empty {});
        return ReadinessState::release_completion_source(source_key, fd);
    }
    auto cancel_operation(u64 key) -> io::Result<empty> {
        if (m_ring.is_some()) return m_ring->deref_mut()->cancel_operation(key);
        return ReadinessState::cancel_operation(key);
    }
    auto begin_shutdown() noexcept -> empty {
        if (m_ring.is_some()) return m_ring->deref_mut()->begin_shutdown();
        return ReadinessState::begin_shutdown();
    }
    auto has_pending_operations() const noexcept -> bool {
        if (m_ring.is_some()) return m_ring->deref()->has_pending_operations();
        return ReadinessState::has_pending_operations();
    }

    auto wait(WaitMode mode, Option<time::Duration> next_timer) -> io::Result<Batch> {
        auto batch = Batch {};
        drain_ready_events(batch);
        if (! batch.is_empty()) return Ok(rstd::move(batch));

        if (m_ring.is_some()) {
            auto submitted = m_ring->deref_mut()->flush();
            if (submitted.is_err()) return Err(rstd::move(submitted).unwrap_err_unchecked());
            if (m_ring->deref_mut()->drain(m_ready_events)) {
                drain_ready_events(batch);
                if (! batch.is_empty()) return Ok(rstd::move(batch));
            }
        }

        auto armed = arm_timer(next_timer);
        if (armed.is_err()) return Err(rstd::move(armed).unwrap_err_unchecked());

        auto wait_ms = mode == WaitMode::Immediate ? 0 : -1;
        int  count {};
        do {
            count = libc::epoll_wait(
                m_poll_fd.as_raw_fd(), m_events, static_cast<int>(Batch::capacity()), wait_ms);
        } while (count < 0 && libc::get_errno() == libc::EINTR);
        if (count < 0) return Err(Error::last_os_error());

        for (int i = 0; i < count; ++i) {
            auto& event = m_events[i];
            if (event.data.u64 == WAKE_KEY) {
                drain_counter(m_wake_fd.as_raw_fd());
                m_ready_events.push(Event::wake());
                continue;
            }
            if (event.data.u64 == TIMER_KEY) {
                m_armed_timer = None<time::Duration>();
                drain_counter(m_timer_fd.as_raw_fd());
                continue;
            }
            if (event.data.u64 == RING_KEY) {
                drain_counter(m_ring->deref()->event_fd());
                (void)m_ring->deref_mut()->drain(m_ready_events);
                continue;
            }

            auto refreshed = dispatch_ready(event.data.u64, ready_from_native(event.events));
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
    auto poll_fd = libc::epoll_create1(libc::EPOLL_CLOEXEC);
    if (poll_fd < 0) return Err(Error::last_os_error());
    auto owned_poll = os::fd::OwnedFd::from_raw_fd(poll_fd);

    auto wake_fd = libc::eventfd(0, libc::EFD_NONBLOCK | libc::EFD_CLOEXEC);
    if (wake_fd < 0) return Err(Error::last_os_error());
    auto owned_wake = os::fd::OwnedFd::from_raw_fd(wake_fd);
    auto wake_send  = owned_wake.try_clone();
    if (wake_send.is_err()) return Err(rstd::move(wake_send).unwrap_err_unchecked());

    auto timer_fd =
        libc::timerfd_create(libc::CLOCK_MONOTONIC, libc::TFD_NONBLOCK | libc::TFD_CLOEXEC);
    if (timer_fd < 0) return Err(Error::last_os_error());
    auto owned_timer = os::fd::OwnedFd::from_raw_fd(timer_fd);

    auto event     = libc::epoll_event {};
    event.events   = libc::EPOLLIN;
    event.data.u64 = WAKE_KEY;
    if (libc::epoll_ctl(poll_fd, libc::EPOLL_CTL_ADD, wake_fd, &event) < 0) {
        return Err(Error::last_os_error());
    }

    auto timer_event     = libc::epoll_event {};
    timer_event.events   = libc::EPOLLIN;
    timer_event.data.u64 = TIMER_KEY;
    if (libc::epoll_ctl(poll_fd, libc::EPOLL_CTL_ADD, timer_fd, &timer_event) < 0) {
        return Err(Error::last_os_error());
    }

    auto ring = Option<Box<uring::Driver>> {};
    if (preference != BackendPreference::ReadinessEmulationRequired) {
        auto initialized_ring = uring::Driver::make();
        if (initialized_ring.is_ok()) {
            auto ready_ring     = rstd::move(initialized_ring).unwrap_unchecked();
            auto ring_event     = libc::epoll_event {};
            ring_event.events   = libc::EPOLLIN;
            ring_event.data.u64 = RING_KEY;
            if (libc::epoll_ctl(
                    poll_fd, libc::EPOLL_CTL_ADD, ready_ring.deref()->event_fd(), &ring_event) ==
                0) {
                ring = Some(rstd::move(ready_ring));
            } else if (preference == BackendPreference::NativeCompletionRequired) {
                return Err(Error::last_os_error());
            }
        } else if (preference == BackendPreference::NativeCompletionRequired) {
            return Err(rstd::move(initialized_ring).unwrap_err_unchecked());
        }
    }

    auto wake_state = Arc<WakeState>::make(rstd::move(wake_send).unwrap_unchecked());
    return Ok(PollInit {
        Poller { rstd::move(owned_poll),
                 rstd::move(owned_wake),
                 rstd::move(owned_timer),
                 rstd::move(ring) },
        PollWake { rstd::move(wake_state) },
    });
}

} // namespace rstd::sys::pal::linux::poll

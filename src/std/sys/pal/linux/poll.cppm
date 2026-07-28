module;
#include <rstd/macro.hpp>
#if RSTD_OS_LINUX
#include <errno.h>
#endif

export module rstd:sys.pal.linux.poll;
export import :sys.pal.poll.types;

#if RSTD_OS_LINUX
import :sys.libc;
import rstd.alloc;

namespace libc = rstd::sys::libc;

namespace rstd::sys::pal::linux::poll
{

using ::alloc::sync::Arc;
using ::alloc::vec::Vec;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::sys::pal::poll::Batch;
using rstd::sys::pal::poll::Capabilities;
using rstd::sys::pal::poll::Capability;
using rstd::sys::pal::poll::Event;
using rstd::sys::pal::poll::Interest;
using rstd::sys::pal::poll::Operation;
using rstd::sys::pal::poll::OperationKind;
using rstd::sys::pal::poll::Ready;
using rstd::sys::pal::poll::SocketAddress;
using rstd::sys::pal::poll::SourceKind;
using rstd::sys::pal::poll::WaitMode;

inline constexpr rstd::uint64_t WAKE_KEY  = 0;
inline constexpr rstd::uint64_t TIMER_KEY = rstd::uint64_t(-1);

struct WakeState {
    os::fd::OwnedFd fd;

    explicit WakeState(os::fd::OwnedFd fd): fd(rstd::move(fd)) {}
};

struct LinuxSource {
    os::fd::RawFd  fd;
    rstd::uint64_t native_key {};
    Option<u64>    readiness_key {};
    Interest       public_interest {};
    Vec<Operation> reads;
    Vec<Operation> writes;
    bool           registered { false };

    LinuxSource(os::fd::RawFd fd, rstd::uint64_t native_key)
        : fd(fd),
          native_key(native_key),
          reads(Vec<Operation>::make()),
          writes(Vec<Operation>::make()) {}
};

struct OperationAttempt {
    bool                          pending { false };
    isize                         result {};
    os::fd::RawFd                 resource { os::fd::INVALID_RAW_FD };
    Option<io::error::RawOsError> error {};
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

export class Poller {
    os::fd::OwnedFd   m_poll_fd;
    os::fd::OwnedFd   m_wake_fd;
    os::fd::OwnedFd   m_timer_fd;
    libc::epoll_event m_events[Batch::capacity()] {};
    Vec<LinuxSource>  m_sources;
    Vec<Event>        m_ready_events;
    rstd::uint64_t    m_next_source_key { 1 };

    auto next_source_key() noexcept -> rstd::uint64_t {
        if (m_next_source_key == WAKE_KEY || m_next_source_key == TIMER_KEY) {
            m_next_source_key = 1;
        }
        auto key = m_next_source_key++;
        return key;
    }

    static auto native_interest(Interest interest) noexcept -> rstd::uint32_t {
        rstd::uint32_t events {};
        if (interest.is_readable()) {
            events |= libc::EPOLLIN;
            if (libc::HAS_EPOLLRDHUP) events |= libc::EPOLLRDHUP;
        }
        if (interest.is_writable()) events |= libc::EPOLLOUT;
        return events;
    }

    struct NativeSocketAddress {
        libc::sockaddr_storage storage {};
        libc::socklen_t        len {};
    };

    static auto native_address(const SocketAddress& address) noexcept -> NativeSocketAddress {
        auto out = NativeSocketAddress {};
        if (! address.ipv6) {
            auto native            = libc::sockaddr_in {};
            native.sin_family      = libc::AF_INET;
            native.sin_port        = libc::htons(address.port.to_primitive());
            auto bits              = (rstd::uint32_t(address.octets[0].to_primitive()) << 24) |
                                     (rstd::uint32_t(address.octets[1].to_primitive()) << 16) |
                                     (rstd::uint32_t(address.octets[2].to_primitive()) << 8) |
                                     rstd::uint32_t(address.octets[3].to_primitive());
            native.sin_addr.s_addr = libc::htonl(bits);
            *reinterpret_cast<libc::sockaddr_in*>(&out.storage) = native;
            out.len                                             = sizeof(native);
            return out;
        }
        auto native          = libc::sockaddr_in6 {};
        native.sin6_family   = libc::AF_INET6;
        native.sin6_port     = libc::htons(address.port.to_primitive());
        native.sin6_flowinfo = libc::htonl(address.flowinfo.to_primitive());
        native.sin6_scope_id = address.scope_id.to_primitive();
        for (rstd::size_t i = 0; i < 16; ++i) {
            libc::set_in6_addr_octet(
                native.sin6_addr, static_cast<unsigned int>(i), address.octets[i].to_primitive());
        }
        *reinterpret_cast<libc::sockaddr_in6*>(&out.storage) = native;
        out.len                                              = sizeof(native);
        return out;
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

    static void drain(os::fd::RawFd fd) noexcept {
        auto value = rstd::uint64_t {};
        while (libc::read(fd, &value, sizeof(value)) > 0) {
        }
    }

    static auto duration_to_timespec(time::Duration duration) noexcept -> libc::timespec {
        return libc::timespec {
            .tv_sec  = static_cast<libc::time_t>(duration.as_secs().to_primitive()),
            .tv_nsec = static_cast<long>(duration.subsec_nanos().to_primitive()),
        };
    }

    auto arm_timer(Option<time::Duration> timeout) -> io::Result<empty> {
        auto spec = libc::itimerspec_t {};
        if (timeout.is_some()) {
            auto duration = *timeout;
            if (duration.is_zero()) duration = time::Duration::from_nanos(u64(1));
            spec.it_value = duration_to_timespec(duration);
        }
        if (libc::timerfd_settime(m_timer_fd.as_raw_fd(), 0, &spec, nullptr) < 0) {
            return Err(Error::last_os_error());
        }
        return Ok(empty {});
    }

    auto find_source(os::fd::RawFd fd) noexcept -> LinuxSource* {
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            if (m_sources[usize(i)].fd == fd) return rstd::addressof(m_sources[usize(i)]);
        }
        return nullptr;
    }

    auto find_source_by_native_key(rstd::uint64_t key) noexcept -> LinuxSource* {
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            if (m_sources[usize(i)].native_key == key) {
                return rstd::addressof(m_sources[usize(i)]);
            }
        }
        return nullptr;
    }

    auto source_for(os::fd::RawFd fd) -> LinuxSource& {
        auto* source = find_source(fd);
        if (source != nullptr) {
            if (! source->registered && source->public_interest.is_empty() &&
                source->reads.is_empty() && source->writes.is_empty()) {
                source->native_key = next_source_key();
            }
            return *source;
        }
        m_sources.push(LinuxSource { fd, next_source_key() });
        return m_sources[m_sources.len() - usize(1)];
    }

    static auto combined_interest(const LinuxSource& source) noexcept -> Interest {
        auto interest = source.public_interest;
        if (! source.reads.is_empty()) interest.bits |= Interest::READABLE;
        if (! source.writes.is_empty()) interest.bits |= Interest::WRITABLE;
        return interest;
    }

    auto refresh_source(LinuxSource& source) -> io::Result<empty> {
        auto interest = combined_interest(source);
        if (interest.is_empty()) {
            if (source.registered &&
                libc::epoll_ctl(m_poll_fd.as_raw_fd(), libc::EPOLL_CTL_DEL, source.fd, nullptr) <
                    0) {
                return Err(Error::last_os_error());
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

    static auto attempt_operation(Operation& operation) noexcept -> OperationAttempt {
        if (operation.source_kind != SourceKind::Socket) {
            return OperationAttempt {
                .error = Some(io::error::RawOsError(libc::EOPNOTSUPP)),
            };
        }

        while (true) {
            decltype(libc::recv(operation.handle, nullptr, 0, 0)) rc {};
            if (operation.kind == OperationKind::Read) {
                rc = libc::recv(operation.handle,
                                operation.mutable_data,
                                operation.len.to_primitive(),
                                static_cast<int>(operation.flags.to_primitive()));
            } else if (operation.kind == OperationKind::Write) {
                rc = libc::send(operation.handle,
                                operation.const_data,
                                operation.len.to_primitive(),
                                static_cast<int>(operation.flags.to_primitive()) |
                                    libc::MSG_NOSIGNAL);
            } else if (operation.kind == OperationKind::Connect) {
                if (operation.started) {
                    int             error = 0;
                    libc::socklen_t len   = sizeof(error);
                    if (libc::getsockopt(
                            operation.handle, libc::SOL_SOCKET, libc::SO_ERROR, &error, &len) ==
                        0) {
                        if (error == 0) return OperationAttempt {};
                        if (error == libc::EINPROGRESS || error == libc::EALREADY) {
                            return OperationAttempt { .pending = true };
                        }
                        return OperationAttempt {
                            .error = Some(io::error::RawOsError(error)),
                        };
                    }
                    rc = -1;
                } else {
                    auto native       = native_address(operation.address);
                    operation.started = true;
                    rc = libc::connect(operation.handle,
                                       reinterpret_cast<const libc::sockaddr*>(&native.storage),
                                       native.len);
                }
            } else {
                auto native   = NativeSocketAddress {};
                native.len    = sizeof(native.storage);
                auto accepted = libc::accept(operation.handle,
                                             reinterpret_cast<libc::sockaddr*>(&native.storage),
                                             &native.len);
                if (accepted >= 0) {
                    auto flags = libc::fcntl(accepted, libc::F_GETFL, 0);
                    if (flags >= 0 &&
                        libc::fcntl(accepted, libc::F_SETFL, flags | libc::O_NONBLOCK) >= 0) {
                        auto fd_flags = libc::fcntl(accepted, libc::F_GETFD, 0);
                        if (fd_flags >= 0 &&
                            libc::fcntl(accepted, libc::F_SETFD, fd_flags | libc::FD_CLOEXEC) >=
                                0) {
                            return OperationAttempt { .resource = accepted };
                        }
                    }
                    auto error = libc::get_errno();
                    libc::close(accepted);
                    return OperationAttempt {
                        .error = Some(io::error::RawOsError(error)),
                    };
                }
                rc = accepted;
            }
            if (rc >= 0) return OperationAttempt { .result = isize(rc) };

            auto error = libc::get_errno();
            if (error == libc::EINTR) continue;
            if (error == libc::EAGAIN || error == libc::EWOULDBLOCK || error == libc::EINPROGRESS ||
                error == libc::EALREADY) {
                return OperationAttempt { .pending = true };
            }
            if (operation.kind == OperationKind::Connect && error == libc::EISCONN) {
                return OperationAttempt {};
            }
            return OperationAttempt { .error = Some(io::error::RawOsError(error)) };
        }
    }

    void finish_attempt(Operation operation, OperationAttempt attempt) {
        if (attempt.error.is_some()) {
            m_ready_events.push(
                Event::completion_error(operation.operation_key, *attempt.error, operation.flags));
        } else if (attempt.resource != os::fd::INVALID_RAW_FD) {
            m_ready_events.push(Event::completion_resource(
                operation.operation_key, attempt.resource, operation.flags));
        } else {
            m_ready_events.push(
                Event::completion(operation.operation_key, attempt.result, operation.flags));
        }
    }

    void retry_queue(Vec<Operation>& queue) {
        while (! queue.is_empty()) {
            auto attempt = attempt_operation(queue[usize()]);
            if (attempt.pending) return;
            auto operation = queue.remove(usize());
            finish_attempt(rstd::move(operation), rstd::move(attempt));
        }
    }

    void drain_ready_events(Batch& batch) {
        while (batch.len().to_primitive() < Batch::capacity() && ! m_ready_events.is_empty()) {
            batch.push(m_ready_events.remove(usize()));
        }
    }

    auto contains_operation(u64 key) const noexcept -> bool {
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            auto const& source = m_sources[usize(i)];
            for (rstd::size_t j = 0; j < source.reads.len().to_primitive(); ++j) {
                if (source.reads[usize(j)].operation_key == key) return true;
            }
            for (rstd::size_t j = 0; j < source.writes.len().to_primitive(); ++j) {
                if (source.writes[usize(j)].operation_key == key) return true;
            }
        }
        return false;
    }

public:
    Poller(): m_sources(Vec<LinuxSource>::make()), m_ready_events(Vec<Event>::make()) {}
    Poller(const Poller&)                        = delete;
    auto operator=(const Poller&) -> Poller&     = delete;
    Poller(Poller&&) noexcept                    = default;
    auto operator=(Poller&&) noexcept -> Poller& = default;

    Poller(os::fd::OwnedFd poll_fd, os::fd::OwnedFd wake_fd, os::fd::OwnedFd timer_fd)
        : m_poll_fd(rstd::move(poll_fd)),
          m_wake_fd(rstd::move(wake_fd)),
          m_timer_fd(rstd::move(timer_fd)),
          m_sources(Vec<LinuxSource>::make()),
          m_ready_events(Vec<Event>::make()) {}

    static auto init() -> io::Result<PollInit>;

    static auto capabilities() noexcept -> Capabilities {
        return Capabilities::of(Capability::Readiness) | Capability::Completion |
               Capability::Timer | Capability::Wake;
    }

    auto register_readiness(u64 key, os::fd::RawFd fd, Interest interest) -> io::Result<empty> {
        auto& source = source_for(fd);
        if (source.readiness_key.is_some()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        source.readiness_key   = Some(key);
        source.public_interest = interest;
        return refresh_source(source);
    }

    auto update_readiness(u64 key, os::fd::RawFd fd, Interest interest) -> io::Result<empty> {
        auto* source = find_source(fd);
        if (source == nullptr || source->readiness_key != Some(key)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::NotFound }));
        }
        source->public_interest = interest;
        return refresh_source(*source);
    }

    auto deregister_readiness(os::fd::RawFd fd) -> io::Result<empty> {
        auto* source = find_source(fd);
        if (source == nullptr) return Ok(empty {});
        source->readiness_key   = None<u64>();
        source->public_interest = Interest {};
        return refresh_source(*source);
    }

    auto submit_operation(Operation operation) -> io::Result<empty> {
        if (operation.handle == os::fd::INVALID_RAW_FD ||
            contains_operation(operation.operation_key)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        if (operation.source_kind != SourceKind::Socket) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
        }
        auto read_direction =
            operation.kind == OperationKind::Read || operation.kind == OperationKind::Accept;
        auto& queue = read_direction ? source_for(operation.handle).reads
                                     : source_for(operation.handle).writes;
        if (queue.is_empty()) {
            auto attempt = attempt_operation(operation);
            if (! attempt.pending) {
                finish_attempt(rstd::move(operation), rstd::move(attempt));
                return Ok(empty {});
            }
        }
        auto fd = operation.handle;
        queue.push(rstd::move(operation));
        return refresh_source(source_for(fd));
    }

    auto cancel_operation(u64 operation_key) -> io::Result<empty> {
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            auto& source   = m_sources[usize(i)];
            auto* queues[] = { rstd::addressof(source.reads), rstd::addressof(source.writes) };
            for (auto* queue : queues) {
                for (rstd::size_t j = 0; j < queue->len().to_primitive(); ++j) {
                    auto index = usize(j);
                    if ((*queue)[index].operation_key != operation_key) continue;
                    auto operation = queue->remove(index);
                    m_ready_events.push(Event::completion_error(operation.operation_key,
                                                                io::error::RawOsError(ECANCELED),
                                                                operation.flags));
                    return refresh_source(source);
                }
            }
        }
        return Ok(empty {});
    }

    auto begin_shutdown() noexcept -> empty {
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            auto& source = m_sources[usize(i)];
            while (! source.reads.is_empty()) {
                auto operation = source.reads.remove(usize());
                m_ready_events.push(Event::completion_error(
                    operation.operation_key, io::error::RawOsError(ECANCELED), operation.flags));
            }
            while (! source.writes.is_empty()) {
                auto operation = source.writes.remove(usize());
                m_ready_events.push(Event::completion_error(
                    operation.operation_key, io::error::RawOsError(ECANCELED), operation.flags));
            }
            (void)refresh_source(source);
        }
        return empty {};
    }

    auto has_pending_operations() const noexcept -> bool {
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            auto const& source = m_sources[usize(i)];
            if (! source.reads.is_empty() || ! source.writes.is_empty()) return true;
        }
        return false;
    }

    auto wait(WaitMode mode, Option<time::Duration> next_timer) -> io::Result<Batch> {
        auto batch = Batch {};
        drain_ready_events(batch);
        if (! batch.is_empty()) return Ok(rstd::move(batch));

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
                drain(m_wake_fd.as_raw_fd());
                m_ready_events.push(Event::wake());
                continue;
            }
            if (event.data.u64 == TIMER_KEY) {
                drain(m_timer_fd.as_raw_fd());
                continue;
            }

            auto* source = find_source_by_native_key(event.data.u64);
            if (source == nullptr) continue;
            auto ready = ready_from_native(event.events);
            if (source->readiness_key.is_some()) {
                auto public_ready = ready;
                if (! source->public_interest.is_readable()) {
                    public_ready.bits &=
                        static_cast<rstd::uint8_t>(~(Ready::READABLE | Ready::READ_CLOSED));
                }
                if (! source->public_interest.is_writable()) {
                    public_ready.bits &=
                        static_cast<rstd::uint8_t>(~(Ready::WRITABLE | Ready::WRITE_CLOSED));
                }
                if (! public_ready.is_empty()) {
                    m_ready_events.push(Event::readiness(*source->readiness_key, public_ready));
                }
            }
            if (ready.bits & (Ready::READABLE | Ready::READ_CLOSED | Ready::ERROR)) {
                retry_queue(source->reads);
            }
            if (ready.bits & (Ready::WRITABLE | Ready::WRITE_CLOSED | Ready::ERROR)) {
                retry_queue(source->writes);
            }
            auto refreshed = refresh_source(*source);
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

inline auto Poller::init() -> io::Result<PollInit> {
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

    auto wake_state = Arc<WakeState>::make(rstd::move(wake_send).unwrap_unchecked());
    return Ok(PollInit {
        Poller { rstd::move(owned_poll), rstd::move(owned_wake), rstd::move(owned_timer) },
        PollWake { rstd::move(wake_state) },
    });
}

} // namespace rstd::sys::pal::linux::poll
#endif

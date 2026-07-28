module;
#include <rstd/macro.hpp>

export module rstd:sys.pal.linux.poll;
export import :sys.pal.poll.types;

#if RSTD_OS_LINUX
import :sys.libc;
import :sys.pal.linux.io_uring;
import :sys.pal.unix.socket;
import rstd.alloc;

namespace libc        = rstd::sys::libc;
namespace uring       = rstd::sys::pal::linux::io_uring;
namespace unix_socket = rstd::sys::pal::unix::socket;

namespace rstd::sys::pal::linux::poll
{

using ::alloc::sync::Arc;
using ::alloc::boxed::Box;
using ::alloc::vec::Vec;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::sys::pal::poll::Batch;
using rstd::sys::pal::poll::BackendPreference;
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
inline constexpr rstd::uint64_t RING_KEY  = TIMER_KEY - 1;

constexpr auto native_source_key(u32 slot, u32 generation) noexcept -> rstd::uint64_t {
    return (static_cast<rstd::uint64_t>(generation.to_primitive()) << 32) | slot.to_primitive();
}

constexpr auto native_source_slot(rstd::uint64_t key) noexcept -> rstd::uint32_t {
    return static_cast<rstd::uint32_t>(key);
}

constexpr auto native_source_generation(rstd::uint64_t key) noexcept -> rstd::uint32_t {
    return static_cast<rstd::uint32_t>(key >> 32);
}

struct WakeState {
    os::fd::OwnedFd fd;

    explicit WakeState(os::fd::OwnedFd fd): fd(rstd::move(fd)) {}
};

struct EmulatedOperation {
    Operation   operation;
    Option<u64> previous {};
    Option<u64> next {};
    u64         source_key {};
    bool        read_direction { false };
    bool        started { false };

    EmulatedOperation(Operation operation, bool read_direction)
        : operation(rstd::move(operation)),
          source_key(this->operation.source_key()),
          read_direction(read_direction) {}
};

struct OperationQueue {
    Option<u64> head {};
    Option<u64> tail {};
    usize       len {};

    auto is_empty() const noexcept -> bool { return len == usize(); }
};

struct LinuxSource {
    os::fd::RawFd  fd;
    rstd::uint64_t native_key {};
    Option<u64>    completion_source_key {};
    Option<u64>    readiness_key {};
    Interest       public_interest {};
    OperationQueue reads;
    OperationQueue writes;
    bool           registered { false };

    LinuxSource(os::fd::RawFd fd, u32 slot, u32 generation = u32(1))
        : fd(fd), native_key(native_source_key(slot, generation)) {}
};

class EmulatedOperations {
    Vec<Option<EmulatedOperation>> m_slots;
    usize                          m_len {};

    static auto slot(u64 key) noexcept -> rstd::uint32_t {
        return static_cast<rstd::uint32_t>(key.to_primitive());
    }

public:
    EmulatedOperations(): m_slots(Vec<Option<EmulatedOperation>>::make()) {}

    auto get(u64 key) -> EmulatedOperation* {
        auto index = slot(key);
        if (index >= m_slots.len().to_primitive() || m_slots[usize(index)].is_none()) {
            return nullptr;
        }
        auto& record = *m_slots[usize(index)];
        return record.operation.operation_key() == key ? rstd::addressof(record) : nullptr;
    }

    auto install(u64 key, EmulatedOperation operation) -> bool {
        auto index = slot(key);
        while (m_slots.len().to_primitive() <= index) {
            m_slots.push(None<EmulatedOperation>());
        }
        if (m_slots[usize(index)].is_some()) return false;
        m_slots[usize(index)] = Some(rstd::move(operation));
        ++m_len;
        return true;
    }

    auto take(u64 key) -> Option<EmulatedOperation> {
        auto* record = get(key);
        if (record == nullptr) return None<EmulatedOperation>();
        --m_len;
        return m_slots[usize(slot(key))].take();
    }

    auto is_empty() const noexcept -> bool { return m_len == usize(); }
};

struct CompletionSourceIndex {
    u32   generation {};
    usize source_index {};
    bool  active { false };
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
    os::fd::OwnedFd            m_poll_fd;
    os::fd::OwnedFd            m_wake_fd;
    os::fd::OwnedFd            m_timer_fd;
    Option<time::Duration>     m_armed_timer;
    libc::epoll_event          m_events[Batch::capacity()] {};
    Vec<LinuxSource>           m_sources;
    Vec<CompletionSourceIndex> m_completion_sources;
    EmulatedOperations         m_operations;
    Vec<Event>                 m_ready_events;
    usize                      m_ready_cursor {};
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

    auto find_source(os::fd::RawFd fd) noexcept -> LinuxSource* {
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            if (m_sources[usize(i)].fd == fd) return rstd::addressof(m_sources[usize(i)]);
        }
        return nullptr;
    }

    auto find_source_by_native_key(rstd::uint64_t key) noexcept -> LinuxSource* {
        auto slot = native_source_slot(key);
        if (slot >= m_sources.len().to_primitive()) return nullptr;
        auto& source = m_sources[usize(slot)];
        return source.fd != os::fd::INVALID_RAW_FD && source.native_key == key
                   ? rstd::addressof(source)
                   : nullptr;
    }

    auto source_index_for(os::fd::RawFd fd) -> usize {
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            auto& source = m_sources[usize(i)];
            if (source.fd != fd) continue;
            return usize(i);
        }
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            auto& source = m_sources[usize(i)];
            if (source.fd != os::fd::INVALID_RAW_FD ||
                native_source_generation(source.native_key) == rstd::uint32_t(-1)) {
                continue;
            }
            source = LinuxSource { fd, u32(i), u32(native_source_generation(source.native_key)) };
            return usize(i);
        }
        if (m_sources.len().to_primitive() >= rstd::uint32_t(-1)) {
            rstd::panic { "Linux Poll exhausted native source slots" };
        }
        auto index = m_sources.len();
        m_sources.push(LinuxSource { fd, u32(index.to_primitive()) });
        return m_sources.len() - usize(1);
    }

    auto source_for(os::fd::RawFd fd) -> LinuxSource& { return m_sources[source_index_for(fd)]; }

    auto completion_source_for(const Operation& operation) -> io::Result<LinuxSource*> {
        auto source_key = operation.source_key();
        auto slot       = static_cast<rstd::uint32_t>(source_key.to_primitive());
        auto generation = static_cast<rstd::uint32_t>(source_key.to_primitive() >> 32);
        if (generation == 0) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        while (m_completion_sources.len().to_primitive() <= slot) {
            m_completion_sources.push(CompletionSourceIndex {});
        }
        auto& binding = m_completion_sources[usize(slot)];
        if (binding.active) {
            if (binding.generation != u32(generation) || binding.source_index >= m_sources.len()) {
                return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
            }
            auto& source = m_sources[binding.source_index];
            if (source.fd != operation.handle() ||
                source.completion_source_key != Some(source_key)) {
                return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
            }
            return Ok(rstd::addressof(source));
        }

        auto  source_index = source_index_for(operation.handle());
        auto& source       = m_sources[source_index];
        if (source.completion_source_key.is_none()) {
            source.completion_source_key = Some(source_key);
        } else if (source.completion_source_key != Some(source_key)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        binding = CompletionSourceIndex { u32(generation), source_index, true };
        return Ok(rstd::addressof(source));
    }

    auto completion_source(u64 source_key) -> LinuxSource* {
        auto slot       = static_cast<rstd::uint32_t>(source_key.to_primitive());
        auto generation = u32(source_key.to_primitive() >> 32);
        if (slot >= m_completion_sources.len().to_primitive()) return nullptr;
        auto& binding = m_completion_sources[usize(slot)];
        if (! binding.active || binding.generation != generation ||
            binding.source_index >= m_sources.len()) {
            return nullptr;
        }
        return rstd::addressof(m_sources[binding.source_index]);
    }

    static void remove_source_if_idle(LinuxSource& source) {
        if (source.registered || source.readiness_key.is_some() ||
            source.completion_source_key.is_some() || ! source.reads.is_empty() ||
            ! source.writes.is_empty()) {
            return;
        }
        auto slot       = native_source_slot(source.native_key);
        auto generation = native_source_generation(source.native_key);
        source.fd       = os::fd::INVALID_RAW_FD;
        if (generation != rstd::uint32_t(-1)) {
            source.native_key = native_source_key(u32(slot), u32(generation + 1));
        }
    }

    static auto combined_interest(const LinuxSource& source) noexcept -> Interest {
        auto interest = source.public_interest;
        if (! source.reads.is_empty()) interest.bits |= Interest::READABLE;
        if (! source.writes.is_empty()) interest.bits |= Interest::WRITABLE;
        return interest;
    }

    static auto queue_for(LinuxSource& source, bool read_direction) noexcept -> OperationQueue& {
        return read_direction ? source.reads : source.writes;
    }

    void enqueue_operation(LinuxSource& source, u64 key) {
        auto* record = m_operations.get(key);
        if (record == nullptr) rstd::panic { "emulated operation slot is missing" };
        auto& queue      = queue_for(source, record->read_direction);
        record->previous = queue.tail;
        if (queue.tail.is_some()) {
            auto* previous = m_operations.get(*queue.tail);
            if (previous == nullptr) rstd::panic { "emulated operation queue is corrupt" };
            previous->next = Some(key);
        } else {
            queue.head = Some(key);
        }
        queue.tail = Some(key);
        ++queue.len;
    }

    auto take_queued_operation(u64 key) -> Option<EmulatedOperation> {
        auto* record = m_operations.get(key);
        if (record == nullptr) return None<EmulatedOperation>();
        auto* source = completion_source(record->source_key);
        if (source == nullptr) return None<EmulatedOperation>();
        auto& queue = queue_for(*source, record->read_direction);
        if (record->previous.is_some()) {
            auto* previous = m_operations.get(*record->previous);
            if (previous == nullptr) rstd::panic { "emulated operation queue is corrupt" };
            previous->next = record->next;
        } else {
            queue.head = record->next;
        }
        if (record->next.is_some()) {
            auto* next = m_operations.get(*record->next);
            if (next == nullptr) rstd::panic { "emulated operation queue is corrupt" };
            next->previous = record->previous;
        } else {
            queue.tail = record->previous;
        }
        --queue.len;
        return m_operations.take(key);
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

    static auto attempt_operation(EmulatedOperation& record) noexcept -> OperationAttempt {
        auto& operation = record.operation;
        if (operation.source_kind() != SourceKind::Socket) {
            return OperationAttempt {
                .error = Some(io::error::RawOsError(libc::EOPNOTSUPP)),
            };
        }

        while (true) {
            decltype(libc::recv(operation.handle(), nullptr, 0, 0)) rc {};
            if (operation.kind() == OperationKind::Read) {
                rc = libc::recv(operation.handle(),
                                operation.mutable_data(),
                                operation.len().to_primitive(),
                                static_cast<int>(operation.flags().to_primitive()));
            } else if (operation.kind() == OperationKind::Write) {
                rc = libc::send(operation.handle(),
                                operation.const_data(),
                                operation.len().to_primitive(),
                                static_cast<int>(operation.flags().to_primitive()) |
                                    libc::MSG_NOSIGNAL);
            } else if (operation.kind() == OperationKind::Connect) {
                if (record.started) {
                    int             error = 0;
                    libc::socklen_t len   = sizeof(error);
                    if (libc::getsockopt(
                            operation.handle(), libc::SOL_SOCKET, libc::SO_ERROR, &error, &len) ==
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
                    auto native    = unix_socket::addr_to_native(operation.address());
                    record.started = true;
                    rc = libc::connect(operation.handle(),
                                       reinterpret_cast<const libc::sockaddr*>(&native.storage),
                                       native.len);
                }
            } else {
                auto native   = unix_socket::NativeSocketAddr {};
                native.len    = sizeof(native.storage);
                auto accepted = libc::accept(operation.handle(),
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
            if (operation.kind() == OperationKind::Connect && error == libc::EISCONN) {
                return OperationAttempt {};
            }
            return OperationAttempt { .error = Some(io::error::RawOsError(error)) };
        }
    }

    void finish_attempt(EmulatedOperation record, OperationAttempt attempt) {
        auto operation = rstd::move(record.operation);
        if (attempt.error.is_some()) {
            m_ready_events.push(Event::completion_error(
                operation.operation_key(), *attempt.error, operation.flags()));
        } else if (attempt.resource != os::fd::INVALID_RAW_FD) {
            m_ready_events.push(Event::completion_resource(
                operation.operation_key(), attempt.resource, operation.flags()));
        } else {
            m_ready_events.push(
                Event::completion(operation.operation_key(), attempt.result, operation.flags()));
        }
    }

    void retry_queue(OperationQueue& queue) {
        while (! queue.is_empty()) {
            auto  key    = *queue.head;
            auto* record = m_operations.get(key);
            if (record == nullptr) rstd::panic { "emulated operation queue is corrupt" };
            auto attempt = attempt_operation(*record);
            if (attempt.pending) return;
            auto operation = take_queued_operation(key).unwrap_unchecked();
            finish_attempt(rstd::move(operation), rstd::move(attempt));
        }
    }

    void drain_ready_events(Batch& batch) {
        while (batch.len().to_primitive() < Batch::capacity() &&
               m_ready_cursor < m_ready_events.len()) {
            batch.push(rstd::move(m_ready_events[m_ready_cursor]));
            ++m_ready_cursor;
        }
        if (m_ready_cursor == m_ready_events.len()) {
            m_ready_events.clear();
            m_ready_cursor = usize();
        }
    }

public:
    Poller()
        : m_sources(Vec<LinuxSource>::make()),
          m_completion_sources(Vec<CompletionSourceIndex>::make()),
          m_operations(),
          m_ready_events(Vec<Event>::make()) {}
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
          m_sources(Vec<LinuxSource>::make()),
          m_completion_sources(Vec<CompletionSourceIndex>::make()),
          m_operations(),
          m_ready_events(Vec<Event>::make()),
          m_ring(rstd::move(ring)) {}

    static auto init(BackendPreference preference) -> io::Result<PollInit>;

    auto capabilities() const noexcept -> Capabilities {
        auto result = Capabilities::of(Capability::Readiness) | Capability::SocketCompletion |
                      Capability::Timer | Capability::Wake;
        if (m_ring.is_some()) result = result | Capability::FileCompletion;
        return result;
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
        auto refreshed          = refresh_source(*source);
        if (refreshed.is_err()) return refreshed;
        remove_source_if_idle(*source);
        return Ok(empty {});
    }

    auto submit_operation(Operation operation) -> io::Result<empty> {
        if (m_ring.is_some()) {
            return m_ring->deref_mut()->submit_operation(rstd::move(operation));
        }
        if (operation.handle() == os::fd::INVALID_RAW_FD ||
            m_operations.get(operation.operation_key()) != nullptr) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        if (operation.source_kind() != SourceKind::Socket) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
        }
        auto source = completion_source_for(operation);
        if (source.is_err()) return Err(rstd::move(source).unwrap_err_unchecked());
        auto* operation_source = rstd::move(source).unwrap_unchecked();
        auto  read_direction =
            operation.kind() == OperationKind::Read || operation.kind() == OperationKind::Accept;
        auto& queue  = read_direction ? operation_source->reads : operation_source->writes;
        auto  key    = operation.operation_key();
        auto  record = EmulatedOperation { rstd::move(operation), read_direction };
        if (! m_operations.install(key, rstd::move(record))) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        if (queue.is_empty()) {
            auto* installed = m_operations.get(key);
            auto  attempt   = attempt_operation(*installed);
            if (! attempt.pending) {
                auto completed = m_operations.take(key).unwrap_unchecked();
                finish_attempt(rstd::move(completed), rstd::move(attempt));
                return Ok(empty {});
            }
        }
        enqueue_operation(*operation_source, key);
        auto refreshed = refresh_source(*operation_source);
        if (refreshed.is_err()) {
            (void)take_queued_operation(key);
            return refreshed;
        }
        return Ok(empty {});
    }

    auto release_completion_source(u64 source_key, os::fd::RawFd fd) -> io::Result<empty> {
        if (m_ring.is_some()) return Ok(empty {});
        auto* source = completion_source(source_key);
        if (source == nullptr) return Ok(empty {});
        if (source->fd != fd || source->completion_source_key != Some(source_key)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::NotFound }));
        }
        if (! source->reads.is_empty() || ! source->writes.is_empty()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::ResourceBusy }));
        }
        source->completion_source_key = None<u64>();
        auto slot                     = static_cast<rstd::uint32_t>(source_key.to_primitive());
        m_completion_sources[usize(slot)].active = false;
        auto refreshed                           = refresh_source(*source);
        if (refreshed.is_err()) return refreshed;
        remove_source_if_idle(*source);
        return Ok(empty {});
    }

    auto cancel_operation(u64 operation_key) -> io::Result<empty> {
        if (m_ring.is_some()) {
            return m_ring->deref_mut()->cancel_operation(operation_key);
        }
        auto* record = m_operations.get(operation_key);
        if (record == nullptr) return Ok(empty {});
        auto* source = completion_source(record->source_key);
        if (source == nullptr) return Ok(empty {});
        auto removed = take_queued_operation(operation_key);
        if (removed.is_none()) return Ok(empty {});
        auto operation = rstd::move(removed).unwrap_unchecked().operation;
        m_ready_events.push(Event::completion_error(
            operation.operation_key(), io::error::RawOsError(libc::ECANCELED), operation.flags()));
        return refresh_source(*source);
    }

    auto begin_shutdown() noexcept -> empty {
        if (m_ring.is_some()) return m_ring->deref_mut()->begin_shutdown();
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            auto& source = m_sources[usize(i)];
            while (! source.reads.is_empty()) {
                auto operation = rstd::move(
                    take_queued_operation(*source.reads.head).unwrap_unchecked().operation);
                m_ready_events.push(Event::completion_error(operation.operation_key(),
                                                            io::error::RawOsError(libc::ECANCELED),
                                                            operation.flags()));
            }
            while (! source.writes.is_empty()) {
                auto operation = rstd::move(
                    take_queued_operation(*source.writes.head).unwrap_unchecked().operation);
                m_ready_events.push(Event::completion_error(operation.operation_key(),
                                                            io::error::RawOsError(libc::ECANCELED),
                                                            operation.flags()));
            }
            (void)refresh_source(source);
        }
        return empty {};
    }

    auto has_pending_operations() const noexcept -> bool {
        if (m_ring.is_some()) return m_ring->deref()->has_pending_operations();
        return ! m_operations.is_empty();
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
#endif

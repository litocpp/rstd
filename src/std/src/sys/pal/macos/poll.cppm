module;
#include <rstd/macro.hpp>
#include <sys/event.h>
#include <sys/types.h>
#include <time.h>
#include <cstdint>

export module rstd:sys.pal.macos.poll;
export import :sys.pal.poll.types;
export import :io.error;

import :sys.libc;
import :sys.pal.unix.socket;
import rstd.alloc;

namespace rstd::sys::pal::macos::poll
{

namespace libc        = rstd::sys::libc;
namespace unix_socket = rstd::sys::pal::unix::socket;

using ::alloc::sync::Arc;
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

struct MacosSource {
    os::fd::RawFd  fd;
    rstd::uint64_t native_key {};
    Option<u64>    completion_source_key {};
    Option<u64>    readiness_key {};
    Interest       public_interest {};
    OperationQueue reads;
    OperationQueue writes;
    bool           registered_read { false };
    bool           registered_write { false };

    MacosSource(os::fd::RawFd fd, u32 slot, u32 generation = u32(1))
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

    PollWake(const PollWake&)                    = delete;
    auto operator=(const PollWake&) -> PollWake& = delete;
    PollWake(PollWake&&) noexcept                = default;
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
    os::fd::OwnedFd        m_poll_fd;
    os::fd::OwnedFd        m_wake_read;
    Option<time::Duration> m_armed_timer {};
    Vec<MacosSource>       m_sources;
    Vec<CompletionSourceIndex> m_completion_sources;
    EmulatedOperations     m_operations;
    Vec<Event>             m_ready_events;
    usize                  m_ready_cursor {};

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
            EV_SET(&change,
                   static_cast<uintptr_t>(TIMER_KEY),
                   EVFILT_TIMER,
                   EV_DELETE,
                   0,
                   0,
                   nullptr);
        }
        if (::kevent(m_poll_fd.as_raw_fd(), &change, 1, nullptr, 0, nullptr) < 0) {
            return Err(Error::last_os_error());
        }
        m_armed_timer = rstd::move(timeout);
        return Ok(empty {});
    }

    auto find_source(os::fd::RawFd fd) noexcept -> MacosSource* {
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            if (m_sources[usize(i)].fd == fd) return rstd::addressof(m_sources[usize(i)]);
        }
        return nullptr;
    }

    auto find_source_by_native_key(rstd::uint64_t key) noexcept -> MacosSource* {
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
            source = MacosSource { fd, u32(i), u32(native_source_generation(source.native_key)) };
            return usize(i);
        }
        if (m_sources.len().to_primitive() >= rstd::uint32_t(-1)) {
            rstd::panic { "macOS Poll exhausted native source slots" };
        }
        auto index = m_sources.len();
        m_sources.push(MacosSource { fd, u32(index.to_primitive()) });
        return m_sources.len() - usize(1);
    }

    auto source_for(os::fd::RawFd fd) -> MacosSource& { return m_sources[source_index_for(fd)]; }

    auto completion_source_for(const Operation& operation) -> io::Result<MacosSource*> {
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

    auto completion_source(u64 source_key) -> MacosSource* {
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

    static void remove_source_if_idle(MacosSource& source) {
        if (source.registered_read || source.registered_write ||
            source.readiness_key.is_some() || source.completion_source_key.is_some() ||
            ! source.reads.is_empty() || ! source.writes.is_empty()) {
            return;
        }
        auto slot       = native_source_slot(source.native_key);
        auto generation = native_source_generation(source.native_key);
        source.fd       = os::fd::INVALID_RAW_FD;
        if (generation != rstd::uint32_t(-1)) {
            source.native_key = native_source_key(u32(slot), u32(generation + 1));
        }
    }

    static auto combined_interest(const MacosSource& source) noexcept -> Interest {
        auto interest = source.public_interest;
        if (! source.reads.is_empty()) interest.bits |= Interest::READABLE;
        if (! source.writes.is_empty()) interest.bits |= Interest::WRITABLE;
        return interest;
    }

    static auto queue_for(MacosSource& source, bool read_direction) noexcept -> OperationQueue& {
        return read_direction ? source.reads : source.writes;
    }

    void enqueue_operation(MacosSource& source, u64 key) {
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

    auto refresh_source(MacosSource& source) -> io::Result<empty> {
        auto interest = combined_interest(source);
        auto want_read  = interest.is_readable();
        auto want_write = interest.is_writable();

        struct kevent changes[4];
        int           n = 0;
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
        : m_sources(Vec<MacosSource>::make()),
          m_completion_sources(Vec<CompletionSourceIndex>::make()),
          m_operations(),
          m_ready_events(Vec<Event>::make()) {}
    Poller(const Poller&)                    = delete;
    auto operator=(const Poller&) -> Poller& = delete;
    Poller(Poller&&) noexcept                = default;
    auto operator=(Poller&&) noexcept -> Poller& = default;

    Poller(os::fd::OwnedFd poll_fd, os::fd::OwnedFd wake_read)
        : m_poll_fd(rstd::move(poll_fd)),
          m_wake_read(rstd::move(wake_read)),
          m_armed_timer(None()),
          m_sources(Vec<MacosSource>::make()),
          m_completion_sources(Vec<CompletionSourceIndex>::make()),
          m_operations(),
          m_ready_events(Vec<Event>::make()) {}

    static auto init(BackendPreference preference) -> io::Result<PollInit>;

    auto capabilities() const noexcept -> Capabilities {
        return Capabilities::of(Capability::Readiness) | Capability::SocketCompletion |
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
        auto refreshed          = refresh_source(*source);
        if (refreshed.is_err()) return refreshed;
        remove_source_if_idle(*source);
        return Ok(empty {});
    }

    auto submit_operation(Operation operation) -> io::Result<empty> {
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
        return ! m_operations.is_empty();
    }

    auto wait(WaitMode mode, Option<time::Duration> next_timer) -> io::Result<Batch> {
        auto batch = Batch {};
        drain_ready_events(batch);
        if (! batch.is_empty()) return Ok(rstd::move(batch));

        auto armed = arm_timer(next_timer);
        if (armed.is_err()) return Err(rstd::move(armed).unwrap_err_unchecked());

        struct kevent events[Batch::capacity()];
        struct timespec zero {};
        const auto* timeout = mode == WaitMode::Immediate ? &zero : nullptr;

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
            auto& event = events[i];
            auto  native_key =
                rstd::uint64_t(reinterpret_cast<uintptr_t>(event.udata));

            if (event.filter == EVFILT_TIMER && native_key == TIMER_KEY) {
                m_armed_timer = None<time::Duration>();
                continue;
            }
            if (event.filter == EVFILT_READ && native_key == WAKE_KEY) {
                drain_counter(m_wake_read.as_raw_fd());
                m_ready_events.push(Event::wake());
                continue;
            }

            auto* source = find_source_by_native_key(native_key);
            if (source == nullptr) continue;
            auto ready = ready_from_kqueue(event);
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

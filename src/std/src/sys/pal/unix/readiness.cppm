export module rstd:sys.pal.unix.readiness;
export import rstd.sys.pal.poll.types;

import rstd.sys.libc;
import rstd.sys.socket;
import rstd.alloc;

namespace libc        = rstd::sys::libc;
namespace unix_socket = rstd::sys::pal::unix::socket;
using ::alloc::vec::Vec;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::sys::pal::poll::Batch;
using rstd::sys::pal::poll::Event;
using rstd::sys::pal::poll::Interest;
using rstd::sys::pal::poll::Operation;
using rstd::sys::pal::poll::OperationKind;
using rstd::sys::pal::poll::Ready;
using rstd::sys::pal::poll::SourceKind;

export namespace rstd::sys::pal::unix::readiness
{
constexpr auto native_source_key(u32 slot, u32 generation) noexcept -> rstd::uint64_t {
    return (static_cast<rstd::uint64_t>(generation.to_primitive()) << 32) | slot.to_primitive();
}

constexpr auto native_source_slot(rstd::uint64_t key) noexcept -> rstd::uint32_t {
    return static_cast<rstd::uint32_t>(key);
}

constexpr auto native_source_generation(rstd::uint64_t key) noexcept -> rstd::uint32_t {
    return static_cast<rstd::uint32_t>(key >> 32);
}

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

struct SourceState {
    os::fd::RawFd  fd;
    rstd::uint64_t native_key {};
    Option<u64>    completion_source_key {};
    Option<u64>    readiness_key {};
    Interest       public_interest {};
    OperationQueue reads;
    OperationQueue writes;

    SourceState(os::fd::RawFd fd, u32 slot, u32 generation)
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

template<typename Source, typename Backend>
class ReadinessState {
protected:
    Vec<Source>                m_sources;
    Vec<CompletionSourceIndex> m_completion_sources;
    EmulatedOperations         m_operations;
    Vec<Event>                 m_ready_events;
    usize                      m_ready_cursor {};

    auto backend() noexcept -> Backend& { return *static_cast<Backend*>(this); }

    auto find_source(os::fd::RawFd fd) noexcept -> Source* {
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            if (m_sources[usize(i)].fd == fd) return rstd::addressof(m_sources[usize(i)]);
        }
        return nullptr;
    }

    auto find_source_by_native_key(rstd::uint64_t key) noexcept -> Source* {
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
            source = Source { fd, u32(i), u32(native_source_generation(source.native_key)) };
            return usize(i);
        }
        if (m_sources.len().to_primitive() >= rstd::uint32_t(-1)) {
            rstd::panic { "Unix Poll exhausted native source slots" };
        }
        auto index = m_sources.len();
        m_sources.push(Source { fd, u32(index.to_primitive()) });
        return m_sources.len() - usize(1);
    }

    auto source_for(os::fd::RawFd fd) -> Source& { return m_sources[source_index_for(fd)]; }

    auto completion_source_for(const Operation& operation) -> io::Result<Source*> {
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

    auto completion_source(u64 source_key) -> Source* {
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

    static void remove_source_if_idle(Source& source) {
        if (source.is_registered() || source.readiness_key.is_some() ||
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

    static auto combined_interest(const Source& source) noexcept -> Interest {
        auto interest = source.public_interest;
        if (! source.reads.is_empty()) interest.bits |= Interest::READABLE;
        if (! source.writes.is_empty()) interest.bits |= Interest::WRITABLE;
        return interest;
    }

    static auto queue_for(Source& source, bool read_direction) noexcept -> OperationQueue& {
        return read_direction ? source.reads : source.writes;
    }

    void enqueue_operation(Source& source, u64 key) {
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
            } else if (operation.kind() == OperationKind::RecvMessage) {
                rc = libc::recvmsg(
                    operation.handle(), static_cast<libc::msghdr*>(operation.native_message()), 0);
            } else if (operation.kind() == OperationKind::SendMessage) {
                rc = libc::sendmsg(operation.handle(),
                                   static_cast<const libc::msghdr*>(operation.native_message()),
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

    auto dispatch_ready(rstd::uint64_t key, Ready ready) -> io::Result<empty> {
        auto* source = find_source_by_native_key(key);
        if (source == nullptr) return Ok(empty {});
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
            if (! public_ready.is_empty())
                m_ready_events.push(Event::readiness(*source->readiness_key, public_ready));
        }
        if (ready.bits & (Ready::READABLE | Ready::READ_CLOSED | Ready::ERROR))
            retry_queue(source->reads);
        if (ready.bits & (Ready::WRITABLE | Ready::WRITE_CLOSED | Ready::ERROR))
            retry_queue(source->writes);
        return backend().refresh_source(*source);
    }

public:
    ReadinessState()
        : m_sources(Vec<Source>::make()),
          m_completion_sources(Vec<CompletionSourceIndex>::make()),
          m_ready_events(Vec<Event>::make()) {}
    auto register_readiness(u64 key, os::fd::RawFd fd, Interest interest) -> io::Result<empty> {
        auto& source = source_for(fd);
        if (source.readiness_key.is_some()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        source.readiness_key   = Some(key);
        source.public_interest = interest;
        return backend().refresh_source(source);
    }

    auto update_readiness(u64 key, os::fd::RawFd fd, Interest interest) -> io::Result<empty> {
        auto* source = find_source(fd);
        if (source == nullptr || source->readiness_key != Some(key)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::NotFound }));
        }
        source->public_interest = interest;
        return backend().refresh_source(*source);
    }

    auto deregister_readiness(os::fd::RawFd fd) -> io::Result<empty> {
        auto* source = find_source(fd);
        if (source == nullptr) return Ok(empty {});
        source->readiness_key   = None<u64>();
        source->public_interest = Interest {};
        auto refreshed          = backend().refresh_source(*source);
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
        auto  read_direction   = operation.kind() == OperationKind::Read ||
                                 operation.kind() == OperationKind::Accept ||
                                 operation.kind() == OperationKind::RecvMessage;
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
        auto refreshed = backend().refresh_source(*operation_source);
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
        auto refreshed                           = backend().refresh_source(*source);
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
        return backend().refresh_source(*source);
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
            (void)backend().refresh_source(source);
        }
        return empty {};
    }

    auto has_pending_operations() const noexcept -> bool { return ! m_operations.is_empty(); }
};
} // namespace rstd::sys::pal::unix::readiness

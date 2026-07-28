module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <winsock2.h>
#include <windows.h>
#endif

export module rstd:sys.pal.windows.poll;
export import :sys.pal.poll.types;

#if RSTD_OS_WINDOWS
import :sys.pal.windows.socket;
import rstd.alloc;

namespace rstd::sys::pal::windows::poll
{

namespace windows_socket = rstd::sys::pal::windows::socket;

using ::alloc::sync::Arc;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::sys::pal::poll::Batch;
using rstd::sys::pal::poll::Capabilities;
using rstd::sys::pal::poll::Capability;
using rstd::sys::pal::poll::Event;
using rstd::sys::pal::poll::Interest;
using rstd::sys::pal::poll::Operation;
using rstd::sys::pal::poll::OperationKind;
using rstd::sys::pal::poll::SourceKind;
using rstd::sys::pal::poll::WaitMode;
using ::alloc::vec::Vec;

inline constexpr ULONG_PTR WAKE_KEY = 0;

struct PortState {
    HANDLE port { nullptr };

    explicit PortState(HANDLE port): port(port) {}

    PortState(const PortState&)                    = delete;
    auto operator=(const PortState&) -> PortState& = delete;

    ~PortState() {
        if (port != nullptr) CloseHandle(port);
    }
};

struct WindowsOperation {
    OVERLAPPED    overlapped {};
    u64           operation_key {};
    HANDLE        handle { nullptr };
    HANDLE        secondary_handle { nullptr };
    SourceKind    source_kind { SourceKind::File };
    OperationKind kind { OperationKind::Read };
    u32           flags {};

    explicit WindowsOperation(const Operation& operation)
        : operation_key(operation.operation_key),
          handle(operation.handle),
          secondary_handle(operation.secondary_handle),
          source_kind(operation.source_kind),
          kind(operation.kind),
          flags(operation.flags) {
        if (operation.offset.is_some()) {
            auto offset           = operation.offset->to_primitive();
            overlapped.Offset     = static_cast<DWORD>(offset);
            overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
        }
    }
};

static_assert(offsetof(WindowsOperation, overlapped) == 0);

struct AssociatedSource {
    HANDLE handle { INVALID_HANDLE_VALUE };
    u64    source_key {};
};

export class PollWake {
    Arc<PortState> m_state;

public:
    explicit PollWake(Arc<PortState> state): m_state(rstd::move(state)) {}

    PollWake(const PollWake&)                        = delete;
    auto operator=(const PollWake&) -> PollWake&     = delete;
    PollWake(PollWake&&) noexcept                    = default;
    auto operator=(PollWake&&) noexcept -> PollWake& = default;

    auto clone() const -> PollWake { return PollWake { m_state.clone() }; }

    auto wake() const -> io::Result<empty> {
        if (! PostQueuedCompletionStatus(m_state->port, 0, WAKE_KEY, nullptr)) {
            return Err(Error::last_os_error());
        }
        return Ok(empty {});
    }
};

export struct PollInit;

export class Poller {
    Option<Arc<PortState>> m_state {};
    Vec<WindowsOperation*> m_operations;
    Vec<AssociatedSource>  m_sources;

    static auto timeout_millis(WaitMode mode, Option<time::Duration> next_timer) noexcept -> DWORD {
        if (mode == WaitMode::Immediate) return 0;
        if (next_timer.is_none()) return INFINITE;

        auto duration = *next_timer;
        auto millis   = duration.as_millis().to_primitive();
        if (! duration.is_zero() && millis == 0) millis = 1;
        if (millis >= static_cast<rstd::uint64_t>(INFINITE)) return INFINITE - 1;
        return static_cast<DWORD>(millis);
    }

    auto port() const noexcept -> HANDLE { return m_state->deref()->port; }

    auto find_operation(u64 key) noexcept -> WindowsOperation* {
        for (rstd::size_t i = 0; i < m_operations.len().to_primitive(); ++i) {
            auto* operation = m_operations[usize(i)];
            if (operation->operation_key == key) return operation;
        }
        return nullptr;
    }

    auto find_source(HANDLE handle) noexcept -> AssociatedSource* {
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            if (m_sources[usize(i)].handle == handle) {
                return rstd::addressof(m_sources[usize(i)]);
            }
        }
        return nullptr;
    }

    void remove_operation(WindowsOperation* operation) noexcept {
        for (rstd::size_t i = 0; i < m_operations.len().to_primitive(); ++i) {
            if (m_operations[usize(i)] != operation) continue;
            m_operations.remove(usize(i));
            return;
        }
    }

public:
    Poller():
        m_operations(Vec<WindowsOperation*>::make()), m_sources(Vec<AssociatedSource>::make()) {}
    Poller(const Poller&)                        = delete;
    auto operator=(const Poller&) -> Poller&     = delete;
    Poller(Poller&&) noexcept                    = default;
    auto operator=(Poller&&) noexcept -> Poller& = default;

    explicit Poller(Arc<PortState> state)
        : m_state(Some(rstd::move(state))),
          m_operations(Vec<WindowsOperation*>::make()),
          m_sources(Vec<AssociatedSource>::make()) {}

    static auto init() -> io::Result<PollInit>;

    static auto capabilities() noexcept -> Capabilities {
        return Capabilities::of(Capability::Completion) | Capability::Timer | Capability::Wake;
    }

    auto register_readiness(u64, os::fd::RawFd, Interest) -> io::Result<empty> {
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
    }

    auto update_readiness(u64, os::fd::RawFd, Interest) -> io::Result<empty> {
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
    }

    auto deregister_readiness(os::fd::RawFd) -> io::Result<empty> {
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
    }

    auto submit_operation(Operation operation) -> io::Result<empty> {
        if (m_state.is_none() || operation.handle == os::fd::INVALID_RAW_FD ||
            find_operation(operation.operation_key) != nullptr) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        if ((operation.kind == OperationKind::Read || operation.kind == OperationKind::Write) &&
            operation.len.to_primitive() > static_cast<rstd::size_t>(MAXDWORD)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        if (operation.source_kind != SourceKind::Socket && operation.kind != OperationKind::Read &&
            operation.kind != OperationKind::Write) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
        }

        auto* source = find_source(operation.handle);
        if (source == nullptr || source->source_key != operation.source_key) {
            auto associated =
                CreateIoCompletionPort(operation.handle,
                                       port(),
                                       static_cast<ULONG_PTR>(operation.source_key.to_primitive()),
                                       0);
            if (associated == nullptr) {
                auto error = GetLastError();
                if (source == nullptr || error != ERROR_INVALID_PARAMETER) {
                    return Err(Error::from_raw_os_error(i32(error)));
                }
            } else if (associated != port()) {
                return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
            }
            if (source == nullptr) {
                m_sources.push(AssociatedSource { operation.handle, operation.source_key });
            } else {
                source->source_key = operation.source_key;
            }
        }

        auto* record = new WindowsOperation { operation };
        if (operation.source_kind == SourceKind::Socket) {
            auto socket = reinterpret_cast<SOCKET>(operation.handle);
            if (operation.kind == OperationKind::Read || operation.kind == OperationKind::Write) {
                auto buffer = WSABUF {
                    .len = static_cast<ULONG>(operation.len.to_primitive()),
                    .buf = static_cast<char*>(operation.kind == OperationKind::Read
                                                  ? operation.mutable_data
                                                  : const_cast<void*>(operation.const_data)),
                };
                DWORD transferred {};
                DWORD flags = operation.flags.to_primitive();
                auto  rc    = operation.kind == OperationKind::Read ? WSARecv(socket,
                                                                              &buffer,
                                                                              1,
                                                                              &transferred,
                                                                              &flags,
                                                                              &record->overlapped,
                                                                              nullptr)
                                                                    : WSASend(socket,
                                                                              &buffer,
                                                                              1,
                                                                              &transferred,
                                                                              flags,
                                                                              &record->overlapped,
                                                                              nullptr);
                if (rc != 0) {
                    auto error = WSAGetLastError();
                    if (error != WSA_IO_PENDING) {
                        delete record;
                        return Err(Error::from_raw_os_error(i32(error)));
                    }
                }
            } else if (operation.kind == OperationKind::Connect) {
                auto started = windows_socket::start_connect(
                    operation.handle, operation.address, &record->overlapped);
                if (started.is_err()) {
                    delete record;
                    return Err(rstd::move(started).unwrap_err_unchecked());
                }
            } else {
                if (operation.secondary_handle == os::fd::INVALID_RAW_FD) {
                    delete record;
                    return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
                }
                auto started = windows_socket::start_accept(operation.handle,
                                                            operation.secondary_handle,
                                                            operation.mutable_data,
                                                            operation.len,
                                                            &record->overlapped);
                if (started.is_err()) {
                    delete record;
                    return Err(rstd::move(started).unwrap_err_unchecked());
                }
            }
        } else {
            DWORD transferred {};
            auto  ok = operation.kind == OperationKind::Read
                           ? ReadFile(operation.handle,
                                      operation.mutable_data,
                                      static_cast<DWORD>(operation.len.to_primitive()),
                                      &transferred,
                                      &record->overlapped)
                           : WriteFile(operation.handle,
                                       operation.const_data,
                                       static_cast<DWORD>(operation.len.to_primitive()),
                                       &transferred,
                                       &record->overlapped);
            if (! ok) {
                auto error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    delete record;
                    return Err(Error::from_raw_os_error(i32(error)));
                }
            }
        }

        m_operations.push(rstd::move(record));
        return Ok(empty {});
    }

    auto cancel_operation(u64 operation_key) -> io::Result<empty> {
        auto* operation = find_operation(operation_key);
        if (operation == nullptr) return Ok(empty {});
        if (CancelIoEx(operation->handle, &operation->overlapped)) return Ok(empty {});
        auto error = GetLastError();
        if (error == ERROR_NOT_FOUND) return Ok(empty {});
        return Err(Error::from_raw_os_error(i32(error)));
    }

    auto begin_shutdown() noexcept -> empty {
        for (rstd::size_t i = 0; i < m_operations.len().to_primitive(); ++i) {
            auto* operation = m_operations[usize(i)];
            (void)CancelIoEx(operation->handle, &operation->overlapped);
        }
        return empty {};
    }

    auto has_pending_operations() const noexcept -> bool { return ! m_operations.is_empty(); }

    auto wait(WaitMode mode, Option<time::Duration> next_timer) -> io::Result<Batch> {
        if (m_state.is_none()) return Err(Error::from_kind(ErrorKind { ErrorKind::NotConnected }));

        auto batch   = Batch {};
        auto timeout = timeout_millis(mode, next_timer);

        for (rstd::size_t i = 0; i < Batch::capacity(); ++i) {
            DWORD       transferred {};
            ULONG_PTR   completion_key {};
            OVERLAPPED* overlapped { nullptr };
            auto        ok = GetQueuedCompletionStatus(
                port(), &transferred, &completion_key, &overlapped, i == 0 ? timeout : 0);
            if (! ok) {
                auto error = GetLastError();
                if (overlapped == nullptr && error == WAIT_TIMEOUT) break;
                if (overlapped == nullptr) {
                    return Err(Error::from_raw_os_error(i32(error)));
                }
                auto* operation = reinterpret_cast<WindowsOperation*>(overlapped);
                remove_operation(operation);
                batch.push(Event::completion_error(
                    operation->operation_key, i32(error), operation->flags));
                delete operation;
                continue;
            }

            if (completion_key == WAKE_KEY && overlapped == nullptr) {
                batch.push(Event::wake());
                continue;
            }

            auto* operation = reinterpret_cast<WindowsOperation*>(overlapped);
            remove_operation(operation);
            if (operation->source_kind == SourceKind::Socket &&
                (operation->kind == OperationKind::Read ||
                 operation->kind == OperationKind::Write)) {
                DWORD socket_transferred = transferred;
                DWORD socket_flags       = operation->flags.to_primitive();
                if (! WSAGetOverlappedResult(reinterpret_cast<SOCKET>(operation->handle),
                                             &operation->overlapped,
                                             &socket_transferred,
                                             FALSE,
                                             &socket_flags)) {
                    auto error = WSAGetLastError();
                    batch.push(Event::completion_error(
                        operation->operation_key, i32(error), u32(socket_flags)));
                    delete operation;
                    continue;
                }
                transferred      = socket_transferred;
                operation->flags = u32(socket_flags);
            }
            if (operation->kind == OperationKind::Connect) {
                auto finished = windows_socket::finish_connect(operation->handle);
                if (finished.is_err()) {
                    auto error = rstd::move(finished).unwrap_err_unchecked();
                    batch.push(Event::completion_error(
                        operation->operation_key,
                        error.raw_os_error().unwrap_or(i32(ERROR_INVALID_DATA)),
                        operation->flags));
                    delete operation;
                    continue;
                }
            }
            if (operation->kind == OperationKind::Accept) {
                auto finished =
                    windows_socket::finish_accept(operation->handle, operation->secondary_handle);
                if (finished.is_err()) {
                    auto error = rstd::move(finished).unwrap_err_unchecked();
                    batch.push(Event::completion_error(
                        operation->operation_key,
                        error.raw_os_error().unwrap_or(i32(ERROR_INVALID_DATA)),
                        operation->flags));
                    delete operation;
                    continue;
                }
                batch.push(Event::completion_resource(
                    operation->operation_key, operation->secondary_handle, operation->flags));
                delete operation;
                continue;
            }
            batch.push(
                Event::completion(operation->operation_key, isize(transferred), operation->flags));
            delete operation;
        }
        return Ok(rstd::move(batch));
    }
};

export struct PollInit {
    Poller   poller;
    PollWake wake;

    PollInit(Poller poller, PollWake wake): poller(rstd::move(poller)), wake(rstd::move(wake)) {}
};

inline auto Poller::init() -> io::Result<PollInit> {
    auto port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (port == nullptr) return Err(Error::last_os_error());

    auto state = Arc<PortState>::make(port);
    return Ok(PollInit { Poller { state.clone() }, PollWake { rstd::move(state) } });
}

} // namespace rstd::sys::pal::windows::poll
#endif

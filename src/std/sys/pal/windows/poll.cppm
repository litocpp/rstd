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
using ::alloc::boxed::Box;
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
    u64           source_key {};
    HANDLE        handle { nullptr };
    HANDLE        secondary_handle { nullptr };
    SourceKind    source_kind { SourceKind::File };
    OperationKind kind { OperationKind::Read };
    u32           flags {};

    explicit WindowsOperation(const Operation& operation)
        : operation_key(operation.operation_key()),
          source_key(operation.source_key()),
          handle(operation.handle()),
          secondary_handle(operation.kind() == OperationKind::Accept ? operation.secondary_handle()
                                                                     : os::fd::INVALID_RAW_FD),
          source_kind(operation.source_kind()),
          kind(operation.kind()),
          flags(operation.flags()) {
        if ((operation.kind() == OperationKind::Read || operation.kind() == OperationKind::Write) &&
            operation.offset().is_some()) {
            auto offset           = operation.offset()->to_primitive();
            overlapped.Offset     = static_cast<DWORD>(offset);
            overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
        }
    }
};

static_assert(__builtin_offsetof(WindowsOperation, overlapped) == 0);

struct AssociatedSource {
    HANDLE handle { INVALID_HANDLE_VALUE };
    u32    generation {};
    usize  active_operations {};
    bool   occupied { false };
};

class NativeOperationTable {
    static constexpr rstd::size_t PAGE_CAPACITY = 64;

    struct Page {
        mem::MaybeUninit<WindowsOperation> records[PAGE_CAPACITY];
        rstd::uint32_t                     generations[PAGE_CAPACITY] {};
        bool                               occupied[PAGE_CAPACITY] {};
    };

    Vec<Box<Page>> m_pages;
    usize          m_len {};

    static auto slot(u64 key) noexcept -> rstd::uint32_t {
        return static_cast<rstd::uint32_t>(key.to_primitive());
    }

    static auto generation(u64 key) noexcept -> rstd::uint32_t {
        return static_cast<rstd::uint32_t>(key.to_primitive() >> 32);
    }

    auto page_for(rstd::uint32_t slot_index) -> Page& {
        auto page_index = static_cast<rstd::size_t>(slot_index) / PAGE_CAPACITY;
        while (m_pages.len().to_primitive() <= page_index) {
            m_pages.push(Box<Page>::make());
        }
        return *m_pages[usize(page_index)];
    }

public:
    NativeOperationTable(): m_pages(Vec<Box<Page>>::make()) {}

    ~NativeOperationTable() {
        if (! is_empty()) rstd::panic { "IOCP native operation table was not drained" };
    }

    auto install(u64 key, WindowsOperation operation) -> WindowsOperation* {
        auto slot_index = slot(key);
        auto gen        = generation(key);
        if (gen == 0) return nullptr;
        auto& page   = page_for(slot_index);
        auto  offset = static_cast<rstd::size_t>(slot_index) % PAGE_CAPACITY;
        if (page.occupied[offset]) return nullptr;
        auto& record             = page.records[offset].write(rstd::move(operation));
        page.generations[offset] = gen;
        page.occupied[offset]    = true;
        ++m_len;
        return rstd::addressof(record);
    }

    auto get(u64 key) noexcept -> WindowsOperation* {
        auto slot_index = slot(key);
        auto page_index = static_cast<rstd::size_t>(slot_index) / PAGE_CAPACITY;
        if (page_index >= m_pages.len().to_primitive()) return nullptr;
        auto& page   = *m_pages[usize(page_index)];
        auto  offset = static_cast<rstd::size_t>(slot_index) % PAGE_CAPACITY;
        if (! page.occupied[offset] || page.generations[offset] != generation(key)) return nullptr;
        return page.records[offset].as_mut_ptr();
    }

    auto take(u64 key) -> Option<WindowsOperation> {
        auto* record = get(key);
        if (record == nullptr) return None<WindowsOperation>();
        auto  slot_index = slot(key);
        auto  page_index = static_cast<rstd::size_t>(slot_index) / PAGE_CAPACITY;
        auto& page       = *m_pages[usize(page_index)];
        auto  offset     = static_cast<rstd::size_t>(slot_index) % PAGE_CAPACITY;
        auto  result     = rstd::move(page.records[offset]).assume_init();
        page.records[offset].assume_init_drop();
        page.occupied[offset] = false;
        --m_len;
        return Some(rstd::move(result));
    }

    template<typename F>
    void for_each(F&& function) {
        for (rstd::size_t page_index = 0; page_index < m_pages.len().to_primitive(); ++page_index) {
            auto& page = *m_pages[usize(page_index)];
            for (rstd::size_t offset = 0; offset < PAGE_CAPACITY; ++offset) {
                if (page.occupied[offset]) function(*page.records[offset].as_mut_ptr());
            }
        }
    }

    auto is_empty() const noexcept -> bool { return m_len == usize(); }
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
    NativeOperationTable   m_operations;
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

    auto source(u64 key) noexcept -> AssociatedSource* {
        auto slot       = static_cast<rstd::uint32_t>(key.to_primitive());
        auto generation = u32(key.to_primitive() >> 32);
        if (slot >= m_sources.len().to_primitive()) return nullptr;
        auto& source = m_sources[usize(slot)];
        if (! source.occupied || source.generation != generation) return nullptr;
        return rstd::addressof(source);
    }

    auto ensure_source(u64 key, HANDLE handle) -> io::Result<AssociatedSource*> {
        auto slot       = static_cast<rstd::uint32_t>(key.to_primitive());
        auto generation = u32(key.to_primitive() >> 32);
        if (generation == u32()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        while (m_sources.len().to_primitive() <= slot) {
            m_sources.push(AssociatedSource {});
        }
        auto& source = m_sources[usize(slot)];
        if (source.occupied) {
            if (source.generation != generation || source.handle != handle) {
                return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
            }
            return Ok(rstd::addressof(source));
        }
        for (rstd::size_t i = 0; i < m_sources.len().to_primitive(); ++i) {
            auto& existing = m_sources[usize(i)];
            if (existing.occupied && existing.handle == handle) {
                return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
            }
        }
        auto associated =
            CreateIoCompletionPort(handle, port(), static_cast<ULONG_PTR>(key.to_primitive()), 0);
        if (associated == nullptr) {
            return Err(Error::from_raw_os_error(i32(GetLastError())));
        }
        if (associated != port()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        source = AssociatedSource { handle, generation, usize(), true };
        return Ok(rstd::addressof(source));
    }

    auto take_completed(u64 operation_key) -> Option<WindowsOperation> {
        auto record = m_operations.take(operation_key);
        if (record.is_none()) return record;
        auto* associated = source(record->source_key);
        if (associated != nullptr) --associated->active_operations;
        return record;
    }

public:
    Poller(): m_operations(), m_sources(Vec<AssociatedSource>::make()) {}
    Poller(const Poller&)                        = delete;
    auto operator=(const Poller&) -> Poller&     = delete;
    Poller(Poller&&) noexcept                    = default;
    auto operator=(Poller&&) noexcept -> Poller& = default;

    explicit Poller(Arc<PortState> state)
        : m_state(Some(rstd::move(state))),
          m_operations(),
          m_sources(Vec<AssociatedSource>::make()) {}

    static auto init(BackendPreference preference) -> io::Result<PollInit>;

    auto capabilities() const noexcept -> Capabilities {
        return Capabilities::of(Capability::SocketCompletion) | Capability::FileCompletion |
               Capability::Timer | Capability::Wake;
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
        if (m_state.is_none() || operation.handle() == os::fd::INVALID_RAW_FD ||
            m_operations.get(operation.operation_key()) != nullptr) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        if ((operation.kind() == OperationKind::Read || operation.kind() == OperationKind::Write) &&
            operation.len().to_primitive() > static_cast<rstd::size_t>(MAXDWORD)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        if (operation.source_kind() != SourceKind::Socket &&
            operation.kind() != OperationKind::Read && operation.kind() != OperationKind::Write) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
        }

        auto source = ensure_source(operation.source_key(), operation.handle());
        if (source.is_err()) return Err(rstd::move(source).unwrap_err_unchecked());

        auto* record =
            m_operations.install(operation.operation_key(), WindowsOperation { operation });
        if (record == nullptr) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        if (operation.source_kind() == SourceKind::Socket) {
            auto socket = reinterpret_cast<SOCKET>(operation.handle());
            if (operation.kind() == OperationKind::Read ||
                operation.kind() == OperationKind::Write) {
                auto buffer = WSABUF {
                    .len = static_cast<ULONG>(operation.len().to_primitive()),
                    .buf = static_cast<char*>(operation.kind() == OperationKind::Read
                                                  ? operation.mutable_data()
                                                  : const_cast<void*>(operation.const_data())),
                };
                DWORD transferred {};
                DWORD flags = operation.flags().to_primitive();
                auto  rc    = operation.kind() == OperationKind::Read ? WSARecv(socket,
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
                        (void)m_operations.take(operation.operation_key());
                        return Err(Error::from_raw_os_error(i32(error)));
                    }
                }
            } else if (operation.kind() == OperationKind::Connect) {
                auto started = windows_socket::start_connect(
                    operation.handle(), operation.address(), &record->overlapped);
                if (started.is_err()) {
                    (void)m_operations.take(operation.operation_key());
                    return Err(rstd::move(started).unwrap_err_unchecked());
                }
            } else {
                if (operation.secondary_handle() == os::fd::INVALID_RAW_FD) {
                    (void)m_operations.take(operation.operation_key());
                    return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
                }
                auto started = windows_socket::start_accept(operation.handle(),
                                                            operation.secondary_handle(),
                                                            operation.mutable_data(),
                                                            operation.len(),
                                                            &record->overlapped);
                if (started.is_err()) {
                    (void)m_operations.take(operation.operation_key());
                    return Err(rstd::move(started).unwrap_err_unchecked());
                }
            }
        } else {
            DWORD transferred {};
            auto  ok = operation.kind() == OperationKind::Read
                           ? ReadFile(operation.handle(),
                                      operation.mutable_data(),
                                      static_cast<DWORD>(operation.len().to_primitive()),
                                      &transferred,
                                      &record->overlapped)
                           : WriteFile(operation.handle(),
                                       operation.const_data(),
                                       static_cast<DWORD>(operation.len().to_primitive()),
                                       &transferred,
                                       &record->overlapped);
            if (! ok) {
                auto error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    (void)m_operations.take(operation.operation_key());
                    return Err(Error::from_raw_os_error(i32(error)));
                }
            }
        }

        ++rstd::move(source).unwrap_unchecked()->active_operations;
        return Ok(empty {});
    }

    auto cancel_operation(u64 operation_key) -> io::Result<empty> {
        auto* operation = m_operations.get(operation_key);
        if (operation == nullptr) return Ok(empty {});
        if (CancelIoEx(operation->handle, &operation->overlapped)) return Ok(empty {});
        auto error = GetLastError();
        if (error == ERROR_NOT_FOUND) return Ok(empty {});
        return Err(Error::from_raw_os_error(i32(error)));
    }

    auto release_completion_source(u64 source_key, os::fd::RawFd fd) -> io::Result<empty> {
        auto* associated = source(source_key);
        if (associated == nullptr) return Ok(empty {});
        if (associated->handle != fd) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::NotFound }));
        }
        if (associated->active_operations != usize()) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::ResourceBusy }));
        }
        associated->handle   = INVALID_HANDLE_VALUE;
        associated->occupied = false;
        return Ok(empty {});
    }

    auto begin_shutdown() noexcept -> empty {
        m_operations.for_each([](WindowsOperation& operation) {
            (void)CancelIoEx(operation.handle, &operation.overlapped);
        });
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
                auto  record    = take_completed(operation->operation_key);
                if (record.is_none()) continue;
                auto completed = rstd::move(record).unwrap_unchecked();
                batch.push(
                    Event::completion_error(completed.operation_key, i32(error), completed.flags));
                continue;
            }

            if (completion_key == WAKE_KEY && overlapped == nullptr) {
                batch.push(Event::wake());
                continue;
            }

            auto* operation = reinterpret_cast<WindowsOperation*>(overlapped);
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
                    auto error  = WSAGetLastError();
                    auto record = take_completed(operation->operation_key);
                    if (record.is_none()) continue;
                    auto completed = rstd::move(record).unwrap_unchecked();
                    batch.push(Event::completion_error(
                        completed.operation_key, i32(error), u32(socket_flags)));
                    continue;
                }
                transferred      = socket_transferred;
                operation->flags = u32(socket_flags);
            }
            if (operation->kind == OperationKind::Connect) {
                auto finished = windows_socket::finish_connect(operation->handle);
                if (finished.is_err()) {
                    auto error  = rstd::move(finished).unwrap_err_unchecked();
                    auto record = take_completed(operation->operation_key);
                    if (record.is_none()) continue;
                    auto completed = rstd::move(record).unwrap_unchecked();
                    batch.push(Event::completion_error(
                        completed.operation_key,
                        error.raw_os_error().unwrap_or(i32(ERROR_INVALID_DATA)),
                        completed.flags));
                    continue;
                }
            }
            if (operation->kind == OperationKind::Accept) {
                auto finished =
                    windows_socket::finish_accept(operation->handle, operation->secondary_handle);
                if (finished.is_err()) {
                    auto error  = rstd::move(finished).unwrap_err_unchecked();
                    auto record = take_completed(operation->operation_key);
                    if (record.is_none()) continue;
                    auto completed = rstd::move(record).unwrap_unchecked();
                    batch.push(Event::completion_error(
                        completed.operation_key,
                        error.raw_os_error().unwrap_or(i32(ERROR_INVALID_DATA)),
                        completed.flags));
                    continue;
                }
                auto record = take_completed(operation->operation_key);
                if (record.is_none()) continue;
                auto completed = rstd::move(record).unwrap_unchecked();
                batch.push(Event::completion_resource(
                    completed.operation_key, completed.secondary_handle, completed.flags));
                continue;
            }
            auto record = take_completed(operation->operation_key);
            if (record.is_none()) continue;
            auto completed = rstd::move(record).unwrap_unchecked();
            batch.push(
                Event::completion(completed.operation_key, isize(transferred), completed.flags));
        }
        return Ok(rstd::move(batch));
    }
};

export struct PollInit {
    Poller   poller;
    PollWake wake;

    PollInit(Poller poller, PollWake wake): poller(rstd::move(poller)), wake(rstd::move(wake)) {}
};

inline auto Poller::init(BackendPreference preference) -> io::Result<PollInit> {
    if (preference == BackendPreference::ReadinessEmulationRequired) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
    }
    auto port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (port == nullptr) return Err(Error::last_os_error());

    auto state = Arc<PortState>::make(port);
    return Ok(PollInit { Poller { state.clone() }, PollWake { rstd::move(state) } });
}

} // namespace rstd::sys::pal::windows::poll
#endif

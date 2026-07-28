module;
#include <rstd/macro.hpp>

export module rstd:async.poll;
export import :io.error;
export import :async.readiness;
export import :time;
export import rstd.core;
import :net.socket_addr;
import :os.fd;
import :sys.pal.poll;
import rstd.alloc;

using namespace rstd;
using ::alloc::vec::Vec;
namespace pal_poll = rstd::sys::pal::poll;

namespace rstd::async
{

using IoError     = io::error::Error;
using IoErrorKind = io::error::ErrorKind;

inline constexpr u64 POLL_WAKE_KEY {};
inline constexpr u64 POLL_TIMER_KEY = u64::MAX;

export enum class PollStateKind {
    Active,
    Waiting,
    Draining,
    Closed,
};

export enum class PollTimeout {
    Immediate,
    Infinite,
};

export enum class PollEventKind {
    Wake,
    Readiness,
    Deregistered,
    Completion,
    Timer,
    BackendError,
};

export enum class PollCapability : rstd::uint8_t {
    Readiness  = 1,
    Completion = 2,
    Timer      = 4,
    Wake       = 8,
};

export class PollCapabilities {
    rstd::uint8_t m_bits {};

    explicit constexpr PollCapabilities(rstd::uint8_t bits) noexcept: m_bits(bits) {}

public:
    constexpr PollCapabilities() noexcept = default;

    static constexpr auto none() noexcept -> PollCapabilities { return PollCapabilities {}; }

    static constexpr auto of(PollCapability capability) noexcept -> PollCapabilities {
        return PollCapabilities { static_cast<rstd::uint8_t>(capability) };
    }

    constexpr auto contains(PollCapability capability) const noexcept -> bool {
        auto bit = static_cast<rstd::uint8_t>(capability);
        return (m_bits & bit) == bit;
    }

    friend constexpr auto operator|(PollCapabilities capabilities,
                                    PollCapability   capability) noexcept -> PollCapabilities {
        return PollCapabilities { static_cast<rstd::uint8_t>(
            capabilities.m_bits | static_cast<rstd::uint8_t>(capability)) };
    }
};

export enum class PollKeyKind {
    Registration,
    Operation,
    Timer,
};

export struct PollKey {
    PollKeyKind kind { PollKeyKind::Registration };
    u64         value {};

    constexpr auto is_valid() const noexcept -> bool {
        return value != POLL_WAKE_KEY && value != POLL_TIMER_KEY;
    }

    friend constexpr auto operator==(PollKey, PollKey) noexcept -> bool = default;
};

export enum class PollOperationKind {
    Read,
    Write,
    Connect,
    Accept,
};

export enum class PollSourceKind {
    File,
    Socket,
};

export class PollOperation {
    PollOperationKind m_kind { PollOperationKind::Read };
    PollSourceKind    m_source_kind { PollSourceKind::File };
    os::fd::RawFd     m_fd { os::fd::INVALID_RAW_FD };
    os::fd::RawFd     m_secondary_fd { os::fd::INVALID_RAW_FD };
    void*             m_mut_data { nullptr };
    const void*       m_const_data { nullptr };
    usize             m_len {};
    Option<u64>       m_offset {};
    u32               m_flags {};
    net::SocketAddr   m_address {};
    u64               m_source_key {};

public:
    static auto
    read(os::fd::RawFd fd, void* data, usize len, Option<u64> offset = None(), u32 flags = u32())
        -> PollOperation {
        auto operation       = PollOperation {};
        operation.m_kind     = PollOperationKind::Read;
        operation.m_fd       = fd;
        operation.m_mut_data = data;
        operation.m_len      = len;
        operation.m_offset   = rstd::move(offset);
        operation.m_flags    = flags;
        return operation;
    }

    static auto read_socket(os::fd::RawFd fd, void* data, usize len, u32 flags = u32())
        -> PollOperation {
        auto operation          = read(fd, data, len, None(), flags);
        operation.m_source_kind = PollSourceKind::Socket;
        return operation;
    }

    static auto write(os::fd::RawFd fd,
                      const void*   data,
                      usize         len,
                      Option<u64>   offset = None(),
                      u32           flags  = u32()) -> PollOperation {
        auto operation         = PollOperation {};
        operation.m_kind       = PollOperationKind::Write;
        operation.m_fd         = fd;
        operation.m_const_data = data;
        operation.m_len        = len;
        operation.m_offset     = rstd::move(offset);
        operation.m_flags      = flags;
        return operation;
    }

    static auto write_socket(os::fd::RawFd fd, const void* data, usize len, u32 flags = u32())
        -> PollOperation {
        auto operation          = write(fd, data, len, None(), flags);
        operation.m_source_kind = PollSourceKind::Socket;
        return operation;
    }

    static auto connect_socket(os::fd::RawFd fd, net::SocketAddr address) -> PollOperation {
        auto operation          = PollOperation {};
        operation.m_kind        = PollOperationKind::Connect;
        operation.m_source_kind = PollSourceKind::Socket;
        operation.m_fd          = fd;
        operation.m_address     = address;
        return operation;
    }

    static auto accept_socket(os::fd::RawFd   fd,
                              os::fd::RawFd   accepted_fd,
                              net::SocketAddr address,
                              void*           address_buffer,
                              usize           address_buffer_len) -> PollOperation {
        auto operation           = PollOperation {};
        operation.m_kind         = PollOperationKind::Accept;
        operation.m_source_kind  = PollSourceKind::Socket;
        operation.m_fd           = fd;
        operation.m_secondary_fd = accepted_fd;
        operation.m_address      = address;
        operation.m_mut_data     = address_buffer;
        operation.m_len          = address_buffer_len;
        return operation;
    }

    auto kind() const noexcept -> PollOperationKind { return m_kind; }
    auto source_kind() const noexcept -> PollSourceKind { return m_source_kind; }
    auto fd() const noexcept -> os::fd::RawFd { return m_fd; }
    auto secondary_fd() const noexcept -> os::fd::RawFd { return m_secondary_fd; }
    auto mutable_data() const noexcept -> void* { return m_mut_data; }
    auto const_data() const noexcept -> const void* { return m_const_data; }
    auto len() const noexcept -> usize { return m_len; }
    auto offset() const noexcept -> const Option<u64>& { return m_offset; }
    auto flags() const noexcept -> u32 { return m_flags; }
    auto address() const noexcept -> net::SocketAddr { return m_address; }
    auto source_key() const noexcept -> u64 { return m_source_key; }
    void set_source_key(u64 source_key) noexcept { m_source_key = source_key; }
};

export class PollCompletion {
    isize                 m_result {};
    u32                   m_flags {};
    Option<IoError>       m_error {};
    Option<os::fd::RawFd> m_resource {};

    PollCompletion(isize result, u32 flags, Option<IoError> error, Option<os::fd::RawFd> resource)
        : m_result(result),
          m_flags(flags),
          m_error(rstd::move(error)),
          m_resource(rstd::move(resource)) {}

public:
    static auto success(isize result, u32 flags = u32(), Option<os::fd::RawFd> resource = None())
        -> PollCompletion {
        return PollCompletion { result, flags, None(), rstd::move(resource) };
    }

    static auto failure(IoError error, u32 flags = u32()) -> PollCompletion {
        return PollCompletion { isize(), flags, Some(rstd::move(error)), None() };
    }

    auto is_error() const noexcept -> bool { return m_error.is_some(); }
    auto result() const noexcept -> isize { return m_result; }
    auto flags() const noexcept -> u32 { return m_flags; }
    auto take_error() -> IoError { return m_error.take().unwrap_unchecked(); }
    auto has_resource() const noexcept -> bool { return m_resource.is_some(); }
    auto take_resource() -> os::fd::RawFd { return m_resource.take().unwrap_unchecked(); }
};

export class PollEventData {
    PollEventKind          m_kind { PollEventKind::Wake };
    PollKey                m_key {};
    Option<Ready>          m_readiness {};
    Option<PollCompletion> m_completion {};
    Option<IoError>        m_backend_error {};

    explicit PollEventData(PollEventKind kind, PollKey key = {}): m_kind(kind), m_key(key) {}

public:
    PollEventData(): PollEventData(PollEventKind::Wake) {}

    static auto wake() -> PollEventData { return PollEventData {}; }

    static auto readiness(PollKey key, Ready ready) -> PollEventData {
        auto data        = PollEventData { PollEventKind::Readiness, key };
        data.m_readiness = Some(ready);
        return data;
    }

    static auto deregistered(PollKey key) -> PollEventData {
        return PollEventData { PollEventKind::Deregistered, key };
    }

    static auto deregistered(PollKey key, IoError error) -> PollEventData {
        auto data            = PollEventData { PollEventKind::Deregistered, key };
        data.m_backend_error = Some(rstd::move(error));
        return data;
    }

    static auto completion(PollKey key, PollCompletion completion) -> PollEventData {
        auto data         = PollEventData { PollEventKind::Completion, key };
        data.m_completion = Some(rstd::move(completion));
        return data;
    }

    static auto timer(PollKey key) -> PollEventData {
        return PollEventData { PollEventKind::Timer, key };
    }

    static auto backend_error(PollKey key, IoError error) -> PollEventData {
        auto data            = PollEventData { PollEventKind::BackendError, key };
        data.m_backend_error = Some(rstd::move(error));
        return data;
    }

    auto kind() const noexcept -> PollEventKind { return m_kind; }
    auto key() const noexcept -> PollKey { return m_key; }
    auto readiness() const -> Ready { return *m_readiness; }
    auto take_completion() -> PollCompletion { return m_completion.take().unwrap_unchecked(); }
    auto has_backend_error() const noexcept -> bool { return m_backend_error.is_some(); }
    auto take_backend_error() -> IoError { return m_backend_error.take().unwrap_unchecked(); }
};

export struct RawPollEventOwner;

export struct RawPollEventOwnerVTable {
    using CloneFn    = RawPollEventOwner (*)(voidp);
    using DispatchFn = void (*)(voidp, PollEventData);
    using DropFn     = void (*)(voidp);

    CloneFn    clone;
    DispatchFn dispatch;
    DropFn     drop;
};

export struct RawPollEventOwner {
    voidp                          data { nullptr };
    const RawPollEventOwnerVTable* vtable { nullptr };

    static constexpr auto from_raw_parts(voidp data, const RawPollEventOwnerVTable* vtable) noexcept
        -> RawPollEventOwner {
        return RawPollEventOwner { data, vtable };
    }
};

export class PollEventOwner {
    RawPollEventOwner m_raw {};

    explicit PollEventOwner(RawPollEventOwner raw): m_raw(raw) {}

public:
    PollEventOwner() noexcept                                = default;
    PollEventOwner(const PollEventOwner&)                    = delete;
    auto operator=(const PollEventOwner&) -> PollEventOwner& = delete;

    PollEventOwner(PollEventOwner&& other) noexcept: m_raw(rstd::exchange(other.m_raw, {})) {}

    auto operator=(PollEventOwner&& other) noexcept -> PollEventOwner& {
        if (this != &other) {
            reset();
            m_raw = rstd::exchange(other.m_raw, {});
        }
        return *this;
    }

    ~PollEventOwner() { reset(); }

    static auto from_raw(RawPollEventOwner raw) -> PollEventOwner { return PollEventOwner { raw }; }

    void reset() noexcept {
        if (m_raw.vtable != nullptr) {
            auto current = rstd::exchange(m_raw, {});
            current.vtable->drop(current.data);
        }
    }

    auto clone() const -> PollEventOwner {
        if (m_raw.vtable == nullptr) return PollEventOwner {};
        return PollEventOwner::from_raw(m_raw.vtable->clone(m_raw.data));
    }

    void dispatch(PollEventData data) const {
        if (m_raw.vtable != nullptr) {
            m_raw.vtable->dispatch(m_raw.data, rstd::move(data));
        }
    }

    explicit operator bool() const noexcept { return m_raw.vtable != nullptr; }
};

export class PollEvent {
    PollEventData          m_data;
    Option<PollEventOwner> m_owner;

    PollEvent(PollEventData data, Option<PollEventOwner> owner)
        : m_data(rstd::move(data)), m_owner(rstd::move(owner)) {}

public:
    PollEvent(): m_data(PollEventData::wake()), m_owner(None()) {}

    static auto wake() -> PollEvent { return PollEvent {}; }

    static auto owned(PollEventData data, PollEventOwner owner) -> PollEvent {
        return PollEvent { rstd::move(data), Some(rstd::move(owner)) };
    }

    PollEvent(const PollEvent&)                        = delete;
    auto operator=(const PollEvent&) -> PollEvent&     = delete;
    PollEvent(PollEvent&&) noexcept                    = default;
    auto operator=(PollEvent&&) noexcept -> PollEvent& = default;

    auto kind() const noexcept -> PollEventKind { return m_data.kind(); }
    auto key() const noexcept -> PollKey { return m_data.key(); }

    void dispatch() {
        if (m_owner.is_some()) {
            m_owner->dispatch(rstd::move(m_data));
        }
    }
};

export enum class PollCommandKind {
    RegisterSource,
    UpdateInterest,
    DeregisterSource,
    SubmitOperation,
    CancelOperation,
    ArmTimer,
    CancelTimer,
};

export class PollCommand {
    PollCommandKind        m_kind { PollCommandKind::RegisterSource };
    PollKey                m_key {};
    os::fd::RawFd          m_fd { os::fd::INVALID_RAW_FD };
    Interest               m_interest {};
    Option<PollOperation>  m_operation {};
    time::Instant          m_deadline {};
    Option<PollEventOwner> m_owner {};

    PollCommand(PollCommandKind kind, PollKey key, PollEventOwner owner)
        : m_kind(kind), m_key(key), m_owner(Some(rstd::move(owner))) {}

public:
    PollCommand(const PollCommand&)                        = delete;
    auto operator=(const PollCommand&) -> PollCommand&     = delete;
    PollCommand(PollCommand&&) noexcept                    = default;
    auto operator=(PollCommand&&) noexcept -> PollCommand& = default;

    static auto
    register_source(PollKey key, os::fd::RawFd fd, Interest interest, PollEventOwner owner)
        -> PollCommand {
        auto command = PollCommand { PollCommandKind::RegisterSource, key, rstd::move(owner) };
        command.m_fd = fd;
        command.m_interest = interest;
        return command;
    }

    static auto update_interest(PollKey key, Interest interest, PollEventOwner owner)
        -> PollCommand {
        auto command = PollCommand { PollCommandKind::UpdateInterest, key, rstd::move(owner) };
        command.m_interest = interest;
        return command;
    }

    static auto deregister_source(PollKey key, PollEventOwner owner) -> PollCommand {
        return PollCommand { PollCommandKind::DeregisterSource, key, rstd::move(owner) };
    }

    static auto submit_operation(PollKey key, PollOperation operation, PollEventOwner owner)
        -> PollCommand {
        auto command = PollCommand { PollCommandKind::SubmitOperation, key, rstd::move(owner) };
        command.m_operation = Some(rstd::move(operation));
        return command;
    }

    static auto cancel_operation(PollKey key, PollEventOwner owner) -> PollCommand {
        return PollCommand { PollCommandKind::CancelOperation, key, rstd::move(owner) };
    }

    static auto arm_timer(PollKey key, time::Instant deadline, PollEventOwner owner)
        -> PollCommand {
        auto command       = PollCommand { PollCommandKind::ArmTimer, key, rstd::move(owner) };
        command.m_deadline = deadline;
        return command;
    }

    static auto cancel_timer(PollKey key, PollEventOwner owner) -> PollCommand {
        return PollCommand { PollCommandKind::CancelTimer, key, rstd::move(owner) };
    }

    auto kind() const noexcept -> PollCommandKind { return m_kind; }
    auto key() const noexcept -> PollKey { return m_key; }
    auto fd() const noexcept -> os::fd::RawFd { return m_fd; }
    auto interest() const noexcept -> Interest { return m_interest; }
    auto operation() const -> const PollOperation& { return *m_operation; }
    auto take_operation() -> PollOperation { return m_operation.take().unwrap_unchecked(); }
    auto deadline() const noexcept -> time::Instant { return m_deadline; }
    auto owner() const -> const PollEventOwner& { return *m_owner; }
    auto take_owner() -> PollEventOwner { return m_owner.take().unwrap_unchecked(); }

    auto into_error_event(IoError error) -> PollEvent {
        return PollEvent::owned(PollEventData::backend_error(m_key, rstd::move(error)),
                                take_owner());
    }
};

export enum class PollApplyStatus {
    Accepted,
    Rejected,
    Unsupported,
};

export class PollApplyResult {
    PollApplyStatus     m_status { PollApplyStatus::Accepted };
    Option<PollCommand> m_command {};
    Option<IoError>     m_error {};
    Option<PollEvent>   m_event {};

    explicit PollApplyResult(PollApplyStatus status): m_status(status) {}

    explicit PollApplyResult(PollEvent event)
        : m_status(PollApplyStatus::Accepted), m_event(Some(rstd::move(event))) {}

    PollApplyResult(PollApplyStatus status, PollCommand command, IoError error)
        : m_status(status),
          m_command(Some(rstd::move(command))),
          m_error(Some(rstd::move(error))) {}

public:
    static auto accepted() -> PollApplyResult {
        return PollApplyResult { PollApplyStatus::Accepted };
    }

    static auto accepted(PollEvent event) -> PollApplyResult {
        return PollApplyResult { rstd::move(event) };
    }

    static auto rejected(PollCommand command, IoError error) -> PollApplyResult {
        return PollApplyResult { PollApplyStatus::Rejected,
                                 rstd::move(command),
                                 rstd::move(error) };
    }

    static auto unsupported(PollCommand command) -> PollApplyResult {
        return PollApplyResult { PollApplyStatus::Unsupported,
                                 rstd::move(command),
                                 IoError::from_kind(IoErrorKind { IoErrorKind::Unsupported }) };
    }

    auto status() const noexcept -> PollApplyStatus { return m_status; }
    auto has_event() const noexcept -> bool { return m_event.is_some(); }
    auto take_event() -> PollEvent { return m_event.take().unwrap_unchecked(); }
    auto take_command() -> PollCommand { return m_command.take().unwrap_unchecked(); }
    auto take_error() -> IoError { return m_error.take().unwrap_unchecked(); }
};

export class PollBatch {
    Vec<PollEvent> m_events;

public:
    PollBatch(): m_events(Vec<PollEvent>::make()) {}

    auto is_empty() const noexcept -> bool { return m_events.is_empty(); }
    auto len() const noexcept -> usize { return m_events.len(); }
    void push(PollEvent event) { m_events.push(rstd::move(event)); }

    auto pop_front() -> Option<PollEvent> {
        if (m_events.is_empty()) return None();
        return Some(m_events.remove(usize()));
    }
};

export class Poll;

export class PollWake {
    pal_poll::PollWake m_inner;

    explicit PollWake(pal_poll::PollWake inner): m_inner(rstd::move(inner)) {}

    friend class Poll;

public:
    PollWake(const PollWake&)                        = delete;
    auto operator=(const PollWake&) -> PollWake&     = delete;
    PollWake(PollWake&&) noexcept                    = default;
    auto operator=(PollWake&&) noexcept -> PollWake& = default;

    auto clone() const -> PollWake { return PollWake { m_inner.clone() }; }

    auto wake() const -> io::Result<empty> { return m_inner.wake(); }
};

struct PollRegistration {
    PollKey        key;
    os::fd::RawFd  fd;
    Interest       interest;
    PollEventOwner owner;
    rstd::uint8_t  backend_interest_bits {};
    bool           backend_registered { false };

    PollRegistration(PollKey key, os::fd::RawFd fd, Interest interest, PollEventOwner owner)
        : key(key), fd(fd), interest(interest), owner(rstd::move(owner)) {}
};

struct PollTimer {
    PollKey        key;
    time::Instant  deadline;
    PollEventOwner owner;

    PollTimer(PollKey key, time::Instant deadline, PollEventOwner owner)
        : key(key), deadline(deadline), owner(rstd::move(owner)) {}
};

struct PollPendingOperation {
    PollKey        key;
    PollEventOwner owner;

    PollPendingOperation(PollKey key, PollEventOwner owner): key(key), owner(rstd::move(owner)) {}
};

export class PollState {
    PollStateKind             m_kind { PollStateKind::Closed };
    pal_poll::Poller          m_backend {};
    Vec<PollRegistration>     m_registrations;
    Vec<PollPendingOperation> m_operations;
    Vec<PollTimer>            m_timers;

    explicit PollState(pal_poll::Poller backend)
        : m_kind(PollStateKind::Active),
          m_backend(rstd::move(backend)),
          m_registrations(Vec<PollRegistration>::make()),
          m_operations(Vec<PollPendingOperation>::make()),
          m_timers(Vec<PollTimer>::make()) {}

    friend class Poll;

public:
    PollState()
        : m_registrations(Vec<PollRegistration>::make()),
          m_operations(Vec<PollPendingOperation>::make()),
          m_timers(Vec<PollTimer>::make()) {}
    PollState(const PollState&)                        = delete;
    auto operator=(const PollState&) -> PollState&     = delete;
    PollState(PollState&&) noexcept                    = default;
    auto operator=(PollState&&) noexcept -> PollState& = default;

    auto kind() const noexcept -> PollStateKind { return m_kind; }
};

export struct PollInit {
    PollState state;
    PollWake  wake;

    PollInit(PollState state, PollWake wake): state(rstd::move(state)), wake(rstd::move(wake)) {}
};

export class Poll {
    static auto backend_interest(Interest interest) noexcept -> pal_poll::Interest {
        return pal_poll::Interest { interest.m_bits.to_primitive() };
    }

    static auto backend_ready(pal_poll::Ready ready) noexcept -> Ready {
        return Ready { u8(ready.bits) };
    }

    static auto backend_operation(PollKey key, const PollOperation& operation)
        -> pal_poll::Operation {
        auto kind = pal_poll::OperationKind::Read;
        switch (operation.kind()) {
        case PollOperationKind::Read: kind = pal_poll::OperationKind::Read; break;
        case PollOperationKind::Write: kind = pal_poll::OperationKind::Write; break;
        case PollOperationKind::Connect: kind = pal_poll::OperationKind::Connect; break;
        case PollOperationKind::Accept: kind = pal_poll::OperationKind::Accept; break;
        }
        auto source_address = operation.address();
        auto address        = pal_poll::SocketAddress {
            .ipv6     = source_address.is_ipv6(),
            .port     = source_address.port(),
            .flowinfo = source_address.flowinfo(),
            .scope_id = source_address.scope_id(),
        };
        for (rstd::size_t i = 0; i < 16; ++i) {
            address.octets[i] = source_address.octet(usize(i));
        }
        return pal_poll::Operation {
            .kind             = kind,
            .source_kind      = operation.source_kind() == PollSourceKind::Socket
                                    ? pal_poll::SourceKind::Socket
                                    : pal_poll::SourceKind::File,
            .handle           = operation.fd(),
            .secondary_handle = operation.secondary_fd(),
            .source_key       = operation.source_key(),
            .operation_key    = key.value,
            .mutable_data     = operation.mutable_data(),
            .const_data       = operation.const_data(),
            .len              = operation.len(),
            .offset           = operation.offset().clone(),
            .flags            = operation.flags(),
            .address          = address,
        };
    }

    static auto find_registration(PollState& state, PollKey key) -> PollRegistration* {
        for (rstd::size_t i = 0; i < state.m_registrations.len().to_primitive(); ++i) {
            auto index = usize(i);
            if (state.m_registrations[index].key == key)
                return rstd::addressof(state.m_registrations[index]);
        }
        return nullptr;
    }

    static auto update_registration(PollState&        state,
                                    PollRegistration& registration,
                                    Interest          interest) -> io::Result<empty> {
        auto backend = backend_interest(interest);
        if (backend.bits == registration.backend_interest_bits && registration.backend_registered) {
            registration.interest = interest;
            return Ok(empty {});
        }

        if (backend.is_empty()) {
            if (registration.backend_registered) {
                auto removed = state.m_backend.deregister_readiness(registration.fd);
                if (removed.is_err()) {
                    return Err(rstd::move(removed).unwrap_err_unchecked());
                }
            }
            registration.interest              = interest;
            registration.backend_interest_bits = 0;
            registration.backend_registered    = false;
            return Ok(empty {});
        }

        auto updated =
            registration.backend_registered
                ? state.m_backend.update_readiness(registration.key.value, registration.fd, backend)
                : state.m_backend.register_readiness(
                      registration.key.value, registration.fd, backend);
        if (updated.is_err()) {
            return Err(rstd::move(updated).unwrap_err_unchecked());
        }

        registration.interest              = interest;
        registration.backend_interest_bits = backend.bits;
        registration.backend_registered    = true;
        return Ok(empty {});
    }

    static auto find_operation(PollState& state, PollKey key) -> PollPendingOperation* {
        for (rstd::size_t i = 0; i < state.m_operations.len().to_primitive(); ++i) {
            if (state.m_operations[usize(i)].key == key) {
                return rstd::addressof(state.m_operations[usize(i)]);
            }
        }
        return nullptr;
    }

    static auto take_operation(PollState& state, PollKey key) -> Option<PollPendingOperation> {
        for (rstd::size_t i = 0; i < state.m_operations.len().to_primitive(); ++i) {
            auto index = usize(i);
            if (state.m_operations[index].key == key) {
                return Some(state.m_operations.remove(index));
            }
        }
        return None<PollPendingOperation>();
    }

    static auto next_timer_duration(const PollState& state) -> Option<time::Duration> {
        if (state.m_timers.is_empty()) return None<time::Duration>();

        auto deadline = state.m_timers[usize()].deadline;
        for (rstd::size_t i = 1; i < state.m_timers.len().to_primitive(); ++i) {
            auto index = usize(i);
            if (state.m_timers[index].deadline < deadline)
                deadline = state.m_timers[index].deadline;
        }

        auto now = time::Instant::now();
        return Some(deadline <= now ? time::Duration {} : deadline - now);
    }

    static auto collect_expired_timers(PollState& state, PollBatch& batch) -> io::Result<empty> {
        auto now = time::Instant::now();
        for (rstd::size_t i = 0; i < state.m_timers.len().to_primitive();) {
            auto index = usize(i);
            if (state.m_timers[index].deadline <= now) {
                auto timer = state.m_timers.remove(index);
                batch.push(
                    PollEvent::owned(PollEventData::timer(timer.key), rstd::move(timer.owner)));
            } else {
                ++i;
            }
        }
        return Ok(empty {});
    }

    static void
    append_backend_batch(PollState& state, pal_poll::Batch backend_batch, PollBatch& batch) {
        for (rstd::size_t i = 0; i < backend_batch.len().to_primitive(); ++i) {
            auto& event = backend_batch[usize(i)];
            if (event.kind == pal_poll::EventKind::Wake) {
                batch.push(PollEvent::wake());
                continue;
            }
            if (event.kind == pal_poll::EventKind::Readiness) {
                auto  key          = PollKey { PollKeyKind::Registration, event.source_key };
                auto* registration = find_registration(state, key);
                if (registration != nullptr) {
                    auto ready = backend_ready(event.ready);
                    if (! ready.is_empty()) {
                        batch.push(PollEvent::owned(PollEventData::readiness(key, ready),
                                                    registration->owner.clone()));
                    }
                }
                continue;
            }
            if (event.kind == pal_poll::EventKind::Completion) {
                auto key       = PollKey { PollKeyKind::Operation, event.operation_key };
                auto operation = take_operation(state, key);
                if (operation.is_none()) continue;
                auto pending  = rstd::move(operation).unwrap_unchecked();
                auto resource = event.resource == os::fd::INVALID_RAW_FD ? None<os::fd::RawFd>()
                                                                         : Some(event.resource);
                auto completion =
                    event.error.is_some()
                        ? PollCompletion::failure(IoError::from_raw_os_error(*event.error),
                                                  event.flags)
                        : PollCompletion::success(event.result, event.flags, rstd::move(resource));
                batch.push(PollEvent::owned(PollEventData::completion(key, rstd::move(completion)),
                                            rstd::move(pending.owner)));
            }
        }
    }

public:
    static auto init() -> io::Result<PollInit> {
        auto initialized = pal_poll::Poller::init();
        if (initialized.is_err()) {
            return Err(rstd::move(initialized).unwrap_err_unchecked());
        }
        auto backend = rstd::move(initialized).unwrap_unchecked();
        return Ok(PollInit { PollState { rstd::move(backend.poller) },
                             PollWake { rstd::move(backend.wake) } });
    }

    static auto capabilities(const PollState&) noexcept -> PollCapabilities {
        auto backend = pal_poll::Poller::capabilities();
        auto result  = PollCapabilities::none();
        if (backend.contains(pal_poll::Capability::Readiness)) {
            result = result | PollCapability::Readiness;
        }
        if (backend.contains(pal_poll::Capability::Completion)) {
            result = result | PollCapability::Completion;
        }
        if (backend.contains(pal_poll::Capability::Timer)) {
            result = result | PollCapability::Timer;
        }
        if (backend.contains(pal_poll::Capability::Wake)) {
            result = result | PollCapability::Wake;
        }
        return result;
    }

    static auto apply(PollState& state, PollCommand command) -> PollApplyResult {
        if (state.m_kind == PollStateKind::Closed || ! command.key().is_valid()) {
            return PollApplyResult::rejected(
                rstd::move(command), IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
        }

        switch (command.kind()) {
        case PollCommandKind::RegisterSource: {
            if (command.key().kind != PollKeyKind::Registration ||
                find_registration(state, command.key()) != nullptr) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
            }

            auto registration = PollRegistration {
                command.key(), command.fd(), Interest {}, command.owner().clone()
            };
            auto updated = update_registration(state, registration, command.interest());
            if (updated.is_err()) {
                return PollApplyResult::rejected(rstd::move(command),
                                                 rstd::move(updated).unwrap_err_unchecked());
            }
            state.m_registrations.push(rstd::move(registration));
            return PollApplyResult::accepted();
        }
        case PollCommandKind::UpdateInterest: {
            auto* registration = find_registration(state, command.key());
            if (registration == nullptr) {
                return PollApplyResult::rejected(
                    rstd::move(command), IoError::from_kind(IoErrorKind { IoErrorKind::NotFound }));
            }
            auto updated = update_registration(state, *registration, command.interest());
            if (updated.is_err()) {
                return PollApplyResult::rejected(rstd::move(command),
                                                 rstd::move(updated).unwrap_err_unchecked());
            }
            return PollApplyResult::accepted();
        }
        case PollCommandKind::DeregisterSource: {
            for (rstd::size_t i = 0; i < state.m_registrations.len().to_primitive(); ++i) {
                auto index = usize(i);
                if (state.m_registrations[index].key != command.key()) continue;
                auto updated =
                    update_registration(state, state.m_registrations[index], Interest {});
                if (updated.is_err()) {
                    return PollApplyResult::rejected(rstd::move(command),
                                                     rstd::move(updated).unwrap_err_unchecked());
                }
                state.m_registrations.remove(index);
                return PollApplyResult::accepted(PollEvent::owned(
                    PollEventData::deregistered(command.key()), command.take_owner()));
            }
            return PollApplyResult::accepted(
                PollEvent::owned(PollEventData::deregistered(command.key()), command.take_owner()));
        }
        case PollCommandKind::SubmitOperation: {
            if (command.key().kind != PollKeyKind::Operation) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
            }
            if (find_operation(state, command.key()) != nullptr) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
            }
            auto submitted = state.m_backend.submit_operation(
                backend_operation(command.key(), command.operation()));
            if (submitted.is_err()) {
                return PollApplyResult::rejected(rstd::move(command),
                                                 rstd::move(submitted).unwrap_err_unchecked());
            }
            state.m_operations.push(PollPendingOperation { command.key(), command.take_owner() });
            return PollApplyResult::accepted();
        }
        case PollCommandKind::CancelOperation: {
            if (command.key().kind != PollKeyKind::Operation) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
            }
            auto canceled = state.m_backend.cancel_operation(command.key().value);
            if (canceled.is_err()) {
                return PollApplyResult::rejected(rstd::move(command),
                                                 rstd::move(canceled).unwrap_err_unchecked());
            }
            return PollApplyResult::accepted();
        }
        case PollCommandKind::ArmTimer: {
            if (command.key().kind != PollKeyKind::Timer) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
            }
            for (rstd::size_t i = 0; i < state.m_timers.len().to_primitive(); ++i) {
                if (state.m_timers[usize(i)].key == command.key()) {
                    return PollApplyResult::rejected(
                        rstd::move(command),
                        IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
                }
            }

            state.m_timers.push(
                PollTimer { command.key(), command.deadline(), command.owner().clone() });
            return PollApplyResult::accepted();
        }
        case PollCommandKind::CancelTimer:
            for (rstd::size_t i = 0; i < state.m_timers.len().to_primitive(); ++i) {
                auto index = usize(i);
                if (state.m_timers[index].key != command.key()) continue;
                state.m_timers.remove(index);
                return PollApplyResult::accepted();
            }
            return PollApplyResult::accepted();
        }
        return PollApplyResult::unsupported(rstd::move(command));
    }

    static auto poll(PollState& state, PollTimeout timeout) -> io::Result<PollBatch> {
        if (state.m_kind == PollStateKind::Closed) {
            return Err(IoError::from_kind(IoErrorKind { IoErrorKind::NotConnected }));
        }

        state.m_kind = PollStateKind::Waiting;
        auto waited =
            state.m_backend.wait(timeout == PollTimeout::Immediate ? pal_poll::WaitMode::Immediate
                                                                   : pal_poll::WaitMode::Infinite,
                                 next_timer_duration(state));
        state.m_kind = PollStateKind::Active;
        if (waited.is_err()) return Err(rstd::move(waited).unwrap_err_unchecked());

        auto batch = PollBatch {};
        append_backend_batch(state, rstd::move(waited).unwrap_unchecked(), batch);
        auto collected = collect_expired_timers(state, batch);
        if (collected.is_err()) {
            return Err(rstd::move(collected).unwrap_err_unchecked());
        }
        return Ok(rstd::move(batch));
    }

    static auto shutdown(PollState& state) -> PollBatch {
        auto batch = PollBatch {};
        if (state.m_kind == PollStateKind::Closed) return batch;
        state.m_kind = PollStateKind::Draining;
        (void)state.m_backend.begin_shutdown();
        while (! state.m_operations.is_empty()) {
            auto waited =
                state.m_backend.wait(pal_poll::WaitMode::Infinite, None<time::Duration>());
            if (waited.is_err()) {
                rstd::panic { "async Poll backend failed while draining operations" };
            }
            append_backend_batch(state, rstd::move(waited).unwrap_unchecked(), batch);
        }
        while (! state.m_registrations.is_empty()) {
            auto registration = state.m_registrations.pop().unwrap_unchecked();
            batch.push(
                PollEvent::owned(PollEventData::deregistered(
                                     registration.key,
                                     IoError::from_kind(IoErrorKind { IoErrorKind::NotConnected })),
                                 rstd::move(registration.owner)));
        }
        while (! state.m_timers.is_empty()) {
            auto timer = state.m_timers.pop().unwrap_unchecked();
            batch.push(PollEvent::owned(
                PollEventData::backend_error(
                    timer.key, IoError::from_kind(IoErrorKind { IoErrorKind::NotConnected })),
                rstd::move(timer.owner)));
        }
        state.m_backend = pal_poll::Poller {};
        state.m_kind    = PollStateKind::Closed;
        return batch;
    }
};

} // namespace rstd::async

module;
#include <rstd/macro.hpp>

export module rstd:async.poll;
export import :io.error;
export import :async.readiness;
export import :time;
export import rstd.core;
import :async.blocking_file_completion;
import :async.blocking_pool;
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

enum class IoBackendPreference
{
    Auto,
    NativeCompletionRequired,
    ReadinessEmulationRequired,
};

export enum class PollEventKind {
    Wake,
    Readiness,
    Deregistered,
    Completion,
    SourceReleased,
    Timer,
    BackendError,
};

export enum class PollCapability : rstd::uint8_t {
    Readiness        = 1,
    SocketCompletion = 2,
    FileCompletion   = 4,
    Completion       = SocketCompletion | FileCompletion,
    Timer            = 8,
    Wake             = 16,
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

export struct OperationKey {
    u32 slot {};
    u32 generation {};

    constexpr auto is_valid() const noexcept -> bool { return generation != u32(); }
    constexpr auto encode() const noexcept -> u64 {
        return u64((static_cast<rstd::uint64_t>(generation.to_primitive()) << 32) |
                   slot.to_primitive());
    }
    static constexpr auto decode(u64 value) noexcept -> OperationKey {
        return OperationKey { u32(value.to_primitive()), u32(value.to_primitive() >> 32) };
    }

    friend constexpr auto operator==(OperationKey, OperationKey) noexcept -> bool = default;
};

export struct RegistrationKey {
    u32 slot {};
    u32 generation {};

    constexpr auto is_valid() const noexcept -> bool { return generation != u32(); }
    constexpr auto encode() const noexcept -> u64 {
        return u64((static_cast<rstd::uint64_t>(generation.to_primitive()) << 32) |
                   slot.to_primitive());
    }
    static constexpr auto decode(u64 value) noexcept -> RegistrationKey {
        return RegistrationKey { u32(value.to_primitive()), u32(value.to_primitive() >> 32) };
    }

    friend constexpr auto operator==(RegistrationKey, RegistrationKey) noexcept -> bool = default;
};

export struct TimerKey {
    u32 slot {};
    u32 generation {};

    constexpr auto        is_valid() const noexcept -> bool { return generation != u32(); }
    friend constexpr auto operator==(TimerKey, TimerKey) noexcept -> bool = default;
};

export struct SourceKey {
    u32 slot {};
    u32 generation {};

    constexpr auto is_valid() const noexcept -> bool { return generation != u32(); }
    constexpr auto encode() const noexcept -> u64 {
        return u64((static_cast<rstd::uint64_t>(generation.to_primitive()) << 32) |
                   slot.to_primitive());
    }
    static constexpr auto decode(u64 value) noexcept -> SourceKey {
        return SourceKey { u32(value.to_primitive()), u32(value.to_primitive() >> 32) };
    }

    friend constexpr auto operator==(SourceKey, SourceKey) noexcept -> bool = default;
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
    struct Read {
        void*       data;
        usize       len;
        Option<u64> offset;
        u32         flags;
    };

    struct Write {
        const void* data;
        usize       len;
        Option<u64> offset;
        u32         flags;
    };

    struct Connect {
        net::SocketAddr address;
    };

    struct Accept {
        os::fd::RawFd   accepted_fd;
        net::SocketAddr address;
        void*           address_buffer;
        usize           address_buffer_len;
    };

    using Storage = Choice<choice_case<PollOperationKind::Read, Read>,
                           choice_case<PollOperationKind::Write, Write>,
                           choice_case<PollOperationKind::Connect, Connect>,
                           choice_case<PollOperationKind::Accept, Accept>>;

    PollSourceKind m_source_kind;
    os::fd::RawFd  m_fd;
    SourceKey      m_source_key {};
    Storage        m_storage;

    PollOperation(PollSourceKind source_kind, os::fd::RawFd fd, Storage storage)
        : m_source_kind(source_kind), m_fd(fd), m_storage(rstd::move(storage)) {}

public:
    static auto
    read(os::fd::RawFd fd, void* data, usize len, Option<u64> offset = None(), u32 flags = u32())
        -> PollOperation {
        return PollOperation { PollSourceKind::File,
                               fd,
                               Storage::with<PollOperationKind::Read>(
                                   Read { data, len, rstd::move(offset), flags }) };
    }

    static auto read_socket(os::fd::RawFd fd, void* data, usize len, u32 flags = u32())
        -> PollOperation {
        return PollOperation { PollSourceKind::Socket,
                               fd,
                               Storage::with<PollOperationKind::Read>(
                                   Read { data, len, None<u64>(), flags }) };
    }

    static auto write(os::fd::RawFd fd,
                      const void*   data,
                      usize         len,
                      Option<u64>   offset = None(),
                      u32           flags  = u32()) -> PollOperation {
        return PollOperation { PollSourceKind::File,
                               fd,
                               Storage::with<PollOperationKind::Write>(
                                   Write { data, len, rstd::move(offset), flags }) };
    }

    static auto write_socket(os::fd::RawFd fd, const void* data, usize len, u32 flags = u32())
        -> PollOperation {
        return PollOperation { PollSourceKind::Socket,
                               fd,
                               Storage::with<PollOperationKind::Write>(
                                   Write { data, len, None<u64>(), flags }) };
    }

    static auto connect_socket(os::fd::RawFd fd, net::SocketAddr address) -> PollOperation {
        return PollOperation { PollSourceKind::Socket,
                               fd,
                               Storage::with<PollOperationKind::Connect>(Connect { address }) };
    }

    static auto accept_socket(os::fd::RawFd   fd,
                              os::fd::RawFd   accepted_fd,
                              net::SocketAddr address,
                              void*           address_buffer,
                              usize           address_buffer_len) -> PollOperation {
        return PollOperation { PollSourceKind::Socket,
                               fd,
                               Storage::with<PollOperationKind::Accept>(Accept {
                                   accepted_fd, address, address_buffer, address_buffer_len }) };
    }

    auto kind() const noexcept -> PollOperationKind { return m_storage.which(); }
    auto source_kind() const noexcept -> PollSourceKind { return m_source_kind; }
    auto fd() const noexcept -> os::fd::RawFd { return m_fd; }
    auto secondary_fd() const noexcept -> os::fd::RawFd {
        return m_storage.as<PollOperationKind::Accept>().accepted_fd;
    }
    auto mutable_data() const noexcept -> void* {
        return kind() == PollOperationKind::Read
                   ? m_storage.as<PollOperationKind::Read>().data
                   : m_storage.as<PollOperationKind::Accept>().address_buffer;
    }
    auto const_data() const noexcept -> const void* {
        return m_storage.as<PollOperationKind::Write>().data;
    }
    auto len() const noexcept -> usize {
        switch (kind()) {
        case PollOperationKind::Read: return m_storage.as<PollOperationKind::Read>().len;
        case PollOperationKind::Write: return m_storage.as<PollOperationKind::Write>().len;
        case PollOperationKind::Accept:
            return m_storage.as<PollOperationKind::Accept>().address_buffer_len;
        case PollOperationKind::Connect: return usize();
        }
        rstd::unreachable();
    }
    auto offset() const noexcept -> const Option<u64>& {
        return kind() == PollOperationKind::Read ? m_storage.as<PollOperationKind::Read>().offset
                                                 : m_storage.as<PollOperationKind::Write>().offset;
    }
    auto flags() const noexcept -> u32 {
        switch (kind()) {
        case PollOperationKind::Read: return m_storage.as<PollOperationKind::Read>().flags;
        case PollOperationKind::Write: return m_storage.as<PollOperationKind::Write>().flags;
        case PollOperationKind::Connect:
        case PollOperationKind::Accept: return u32();
        }
        rstd::unreachable();
    }
    auto address() const noexcept -> net::SocketAddr {
        return kind() == PollOperationKind::Connect
                   ? m_storage.as<PollOperationKind::Connect>().address
                   : m_storage.as<PollOperationKind::Accept>().address;
    }
    auto source_key() const noexcept -> SourceKey { return m_source_key; }
    void set_source_key(SourceKey source_key) noexcept { m_source_key = source_key; }
};

export class PollCompletion {
    enum class Tag : rstd::uint8_t
    {
        Success,
        Failure,
    };

    struct Success {
        isize                 result {};
        u32                   flags {};
        Option<os::fd::RawFd> resource {};
    };

    struct Failure {
        IoError error;
        u32     flags {};
    };

    using Storage = Choice<choice_case<Tag::Success, Success>, choice_case<Tag::Failure, Failure>>;

    Storage m_storage;

    explicit PollCompletion(Storage storage): m_storage(rstd::move(storage)) {}

public:
    static auto success(isize result, u32 flags = u32(), Option<os::fd::RawFd> resource = None())
        -> PollCompletion {
        return PollCompletion { Storage::with<Tag::Success>(
            Success { result, flags, rstd::move(resource) }) };
    }

    static auto failure(IoError error, u32 flags = u32()) -> PollCompletion {
        return PollCompletion { Storage::with<Tag::Failure>(Failure { rstd::move(error), flags }) };
    }

    auto is_error() const noexcept -> bool { return m_storage.is<Tag::Failure>(); }
    auto result() const noexcept -> isize { return m_storage.as<Tag::Success>().result; }
    auto flags() const noexcept -> u32 {
        return is_error() ? m_storage.as<Tag::Failure>().flags : m_storage.as<Tag::Success>().flags;
    }
    auto take_error() -> IoError { return rstd::move(m_storage.as<Tag::Failure>().error); }
    auto has_resource() const noexcept -> bool {
        return m_storage.is<Tag::Success>() && m_storage.as<Tag::Success>().resource.is_some();
    }
    auto take_resource() -> os::fd::RawFd {
        return m_storage.as<Tag::Success>().resource.take().unwrap_unchecked();
    }
};

export class PollEventData {
    enum class IdentityTag : rstd::uint8_t
    {
        Registration,
        Operation,
        Timer,
    };

    using Identity = Choice<choice_case<IdentityTag::Registration, RegistrationKey>,
                            choice_case<IdentityTag::Operation, OperationKey>,
                            choice_case<IdentityTag::Timer, TimerKey>>;

    struct Timer {
        TimerKey key;
    };

    struct Readiness {
        RegistrationKey key;
        Ready           ready;
    };

    struct Deregistered {
        RegistrationKey key;
        Option<IoError> error;
    };

    struct Completion {
        OperationKey   key;
        PollCompletion completion;
    };

    struct BackendError {
        Identity key;
        IoError  error;
    };

    struct SourceReleased {
        Option<IoError> error;
    };

    using Storage = Choice<choice_case<PollEventKind::Wake, void>,
                           choice_case<PollEventKind::Readiness, Readiness>,
                           choice_case<PollEventKind::Deregistered, Deregistered>,
                           choice_case<PollEventKind::Completion, Completion>,
                           choice_case<PollEventKind::SourceReleased, SourceReleased>,
                           choice_case<PollEventKind::Timer, Timer>,
                           choice_case<PollEventKind::BackendError, BackendError>>;

    Storage m_storage;

    explicit PollEventData(Storage storage): m_storage(rstd::move(storage)) {}

public:
    PollEventData(): m_storage(Storage::with<PollEventKind::Wake>()) {}

    static auto wake() -> PollEventData { return PollEventData {}; }

    static auto readiness(RegistrationKey key, Ready ready) -> PollEventData {
        return PollEventData { Storage::with<PollEventKind::Readiness>(Readiness { key, ready }) };
    }

    static auto deregistered(RegistrationKey key) -> PollEventData {
        return PollEventData { Storage::with<PollEventKind::Deregistered>(
            Deregistered { key, None<IoError>() }) };
    }

    static auto deregistered(RegistrationKey key, IoError error) -> PollEventData {
        return PollEventData { Storage::with<PollEventKind::Deregistered>(
            Deregistered { key, Some(rstd::move(error)) }) };
    }

    static auto completion(OperationKey key, PollCompletion completion) -> PollEventData {
        return PollEventData { Storage::with<PollEventKind::Completion>(
            Completion { key, rstd::move(completion) }) };
    }

    static auto source_released() -> PollEventData {
        return PollEventData { Storage::with<PollEventKind::SourceReleased>(
            SourceReleased { None<IoError>() }) };
    }

    static auto source_released(IoError error) -> PollEventData {
        return PollEventData { Storage::with<PollEventKind::SourceReleased>(
            SourceReleased { Some(rstd::move(error)) }) };
    }

    static auto timer(TimerKey key) -> PollEventData {
        return PollEventData { Storage::with<PollEventKind::Timer>(Timer { key }) };
    }

    static auto backend_error(RegistrationKey key, IoError error) -> PollEventData {
        return PollEventData { Storage::with<PollEventKind::BackendError>(
            BackendError { Identity::with<IdentityTag::Registration>(key), rstd::move(error) }) };
    }

    static auto backend_error(OperationKey key, IoError error) -> PollEventData {
        return PollEventData { Storage::with<PollEventKind::BackendError>(
            BackendError { Identity::with<IdentityTag::Operation>(key), rstd::move(error) }) };
    }

    static auto backend_error(TimerKey key, IoError error) -> PollEventData {
        return PollEventData { Storage::with<PollEventKind::BackendError>(
            BackendError { Identity::with<IdentityTag::Timer>(key), rstd::move(error) }) };
    }

    auto kind() const noexcept -> PollEventKind { return m_storage.which(); }
    auto registration_key() const noexcept -> RegistrationKey {
        if (m_storage.is<PollEventKind::Readiness>()) {
            return m_storage.as<PollEventKind::Readiness>().key;
        }
        if (m_storage.is<PollEventKind::Deregistered>()) {
            return m_storage.as<PollEventKind::Deregistered>().key;
        }
        return m_storage.as<PollEventKind::BackendError>().key.as<IdentityTag::Registration>();
    }
    auto operation_key() const noexcept -> OperationKey {
        if (m_storage.is<PollEventKind::Completion>()) {
            return m_storage.as<PollEventKind::Completion>().key;
        }
        return m_storage.as<PollEventKind::BackendError>().key.as<IdentityTag::Operation>();
    }
    auto timer_key() const noexcept -> TimerKey {
        if (m_storage.is<PollEventKind::Timer>()) {
            return m_storage.as<PollEventKind::Timer>().key;
        }
        return m_storage.as<PollEventKind::BackendError>().key.as<IdentityTag::Timer>();
    }
    auto readiness() const -> Ready { return m_storage.as<PollEventKind::Readiness>().ready; }
    auto take_completion() -> PollCompletion {
        return rstd::move(m_storage.as<PollEventKind::Completion>().completion);
    }
    auto has_backend_error() const noexcept -> bool {
        if (m_storage.is<PollEventKind::BackendError>()) return true;
        if (m_storage.is<PollEventKind::SourceReleased>()) {
            return m_storage.as<PollEventKind::SourceReleased>().error.is_some();
        }
        return m_storage.is<PollEventKind::Deregistered>() &&
               m_storage.as<PollEventKind::Deregistered>().error.is_some();
    }
    auto take_backend_error() -> IoError {
        if (m_storage.is<PollEventKind::BackendError>()) {
            return rstd::move(m_storage.as<PollEventKind::BackendError>().error);
        }
        if (m_storage.is<PollEventKind::SourceReleased>()) {
            return m_storage.as<PollEventKind::SourceReleased>().error.take().unwrap_unchecked();
        }
        return m_storage.as<PollEventKind::Deregistered>().error.take().unwrap_unchecked();
    }
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
    ReleaseCompletionSource,
    ArmTimer,
    CancelTimer,
};

export class PollCommand {
    enum class OperationOwnerTag : rstd::uint8_t
    {
        Reserved,
        Attached,
    };

    using OperationOwner = Choice<choice_case<OperationOwnerTag::Reserved, void>,
                                  choice_case<OperationOwnerTag::Attached, PollEventOwner>>;

    struct RegisterSource {
        RegistrationKey key;
        os::fd::RawFd   fd;
        Interest        interest;
        PollEventOwner  owner;
    };

    struct UpdateInterest {
        RegistrationKey key;
        Interest        interest;
        PollEventOwner  owner;
    };

    struct RegistrationOwner {
        RegistrationKey key;
        PollEventOwner  owner;
    };

    struct SubmitOperation {
        OperationKey   key;
        PollOperation  operation;
        OperationOwner owner;
    };

    struct ReleaseCompletionSource {
        SourceKey      source_key;
        os::fd::RawFd  fd;
        PollEventOwner owner;
    };

    struct ArmTimer {
        TimerKey       key;
        time::Instant  deadline;
        PollEventOwner owner;
    };

    struct TimerOwner {
        TimerKey       key;
        PollEventOwner owner;
    };

    using Storage =
        Choice<choice_case<PollCommandKind::RegisterSource, RegisterSource>,
               choice_case<PollCommandKind::UpdateInterest, UpdateInterest>,
               choice_case<PollCommandKind::DeregisterSource, RegistrationOwner>,
               choice_case<PollCommandKind::SubmitOperation, SubmitOperation>,
               choice_case<PollCommandKind::CancelOperation, OperationKey>,
               choice_case<PollCommandKind::ReleaseCompletionSource, ReleaseCompletionSource>,
               choice_case<PollCommandKind::ArmTimer, ArmTimer>,
               choice_case<PollCommandKind::CancelTimer, TimerOwner>>;

    Storage m_storage;

    explicit PollCommand(Storage storage): m_storage(rstd::move(storage)) {}

public:
    PollCommand(const PollCommand&)                        = delete;
    auto operator=(const PollCommand&) -> PollCommand&     = delete;
    PollCommand(PollCommand&&) noexcept                    = default;
    auto operator=(PollCommand&&) noexcept -> PollCommand& = default;

    static auto
    register_source(RegistrationKey key, os::fd::RawFd fd, Interest interest, PollEventOwner owner)
        -> PollCommand {
        return PollCommand { Storage::with<PollCommandKind::RegisterSource>(
            RegisterSource { key, fd, interest, rstd::move(owner) }) };
    }

    static auto update_interest(RegistrationKey key, Interest interest, PollEventOwner owner)
        -> PollCommand {
        return PollCommand { Storage::with<PollCommandKind::UpdateInterest>(
            UpdateInterest { key, interest, rstd::move(owner) }) };
    }

    static auto deregister_source(RegistrationKey key, PollEventOwner owner) -> PollCommand {
        return PollCommand { Storage::with<PollCommandKind::DeregisterSource>(
            RegistrationOwner { key, rstd::move(owner) }) };
    }

    static auto submit_operation(OperationKey key, PollOperation operation) -> PollCommand {
        return PollCommand { Storage::with<PollCommandKind::SubmitOperation>(SubmitOperation {
            key, rstd::move(operation), OperationOwner::with<OperationOwnerTag::Reserved>() }) };
    }

    static auto submit_remote_operation(OperationKey   key,
                                        PollOperation  operation,
                                        PollEventOwner owner) -> PollCommand {
        return PollCommand { Storage::with<PollCommandKind::SubmitOperation>(SubmitOperation {
            key,
            rstd::move(operation),
            OperationOwner::with<OperationOwnerTag::Attached>(rstd::move(owner)),
        }) };
    }

    static auto cancel_operation(OperationKey key) -> PollCommand {
        return PollCommand { Storage::with<PollCommandKind::CancelOperation>(key) };
    }

    static auto release_completion_source(SourceKey      source_key,
                                          os::fd::RawFd  fd,
                                          PollEventOwner owner = PollEventOwner {}) -> PollCommand {
        return PollCommand { Storage::with<PollCommandKind::ReleaseCompletionSource>(
            ReleaseCompletionSource { source_key, fd, rstd::move(owner) }) };
    }

    static auto arm_timer(TimerKey key, time::Instant deadline, PollEventOwner owner)
        -> PollCommand {
        return PollCommand { Storage::with<PollCommandKind::ArmTimer>(
            ArmTimer { key, deadline, rstd::move(owner) }) };
    }

    static auto cancel_timer(TimerKey key, PollEventOwner owner) -> PollCommand {
        return PollCommand { Storage::with<PollCommandKind::CancelTimer>(
            TimerOwner { key, rstd::move(owner) }) };
    }

    auto kind() const noexcept -> PollCommandKind { return m_storage.which(); }
    auto registration_key() const noexcept -> RegistrationKey {
        switch (kind()) {
        case PollCommandKind::RegisterSource:
            return m_storage.as<PollCommandKind::RegisterSource>().key;
        case PollCommandKind::UpdateInterest:
            return m_storage.as<PollCommandKind::UpdateInterest>().key;
        case PollCommandKind::DeregisterSource:
            return m_storage.as<PollCommandKind::DeregisterSource>().key;
        default: rstd::unreachable();
        }
    }
    auto operation_key() const noexcept -> OperationKey {
        return kind() == PollCommandKind::SubmitOperation
                   ? m_storage.as<PollCommandKind::SubmitOperation>().key
                   : m_storage.as<PollCommandKind::CancelOperation>();
    }
    auto timer_key() const noexcept -> TimerKey {
        return kind() == PollCommandKind::ArmTimer
                   ? m_storage.as<PollCommandKind::ArmTimer>().key
                   : m_storage.as<PollCommandKind::CancelTimer>().key;
    }
    auto fd() const noexcept -> os::fd::RawFd {
        return kind() == PollCommandKind::RegisterSource
                   ? m_storage.as<PollCommandKind::RegisterSource>().fd
                   : m_storage.as<PollCommandKind::ReleaseCompletionSource>().fd;
    }
    auto interest() const noexcept -> Interest {
        return kind() == PollCommandKind::RegisterSource
                   ? m_storage.as<PollCommandKind::RegisterSource>().interest
                   : m_storage.as<PollCommandKind::UpdateInterest>().interest;
    }
    auto operation() const -> const PollOperation& {
        return m_storage.as<PollCommandKind::SubmitOperation>().operation;
    }
    auto has_operation_owner() const noexcept -> bool {
        return kind() == PollCommandKind::SubmitOperation &&
               m_storage.as<PollCommandKind::SubmitOperation>()
                   .owner.is<OperationOwnerTag::Attached>();
    }
    auto take_operation_owner() -> PollEventOwner {
        return rstd::move(m_storage.as<PollCommandKind::SubmitOperation>()
                              .owner.as<OperationOwnerTag::Attached>());
    }
    auto can_dispatch_error() const noexcept -> bool {
        switch (kind()) {
        case PollCommandKind::SubmitOperation: return has_operation_owner();
        case PollCommandKind::CancelOperation: return false;
        default: return static_cast<bool>(owner());
        }
    }
    auto source_key() const noexcept -> SourceKey {
        return m_storage.as<PollCommandKind::ReleaseCompletionSource>().source_key;
    }
    auto deadline() const noexcept -> time::Instant {
        return m_storage.as<PollCommandKind::ArmTimer>().deadline;
    }
    auto owner() const -> const PollEventOwner& {
        switch (kind()) {
        case PollCommandKind::RegisterSource:
            return m_storage.as<PollCommandKind::RegisterSource>().owner;
        case PollCommandKind::UpdateInterest:
            return m_storage.as<PollCommandKind::UpdateInterest>().owner;
        case PollCommandKind::DeregisterSource:
            return m_storage.as<PollCommandKind::DeregisterSource>().owner;
        case PollCommandKind::SubmitOperation:
        case PollCommandKind::CancelOperation:
            rstd::panic { "operation command owner belongs to the worker operation slot" };
        case PollCommandKind::ReleaseCompletionSource:
            return m_storage.as<PollCommandKind::ReleaseCompletionSource>().owner;
        case PollCommandKind::ArmTimer: return m_storage.as<PollCommandKind::ArmTimer>().owner;
        case PollCommandKind::CancelTimer:
            return m_storage.as<PollCommandKind::CancelTimer>().owner;
        }
        rstd::unreachable();
    }
    auto take_owner() -> PollEventOwner {
        switch (kind()) {
        case PollCommandKind::RegisterSource:
            return rstd::move(m_storage.as<PollCommandKind::RegisterSource>().owner);
        case PollCommandKind::UpdateInterest:
            return rstd::move(m_storage.as<PollCommandKind::UpdateInterest>().owner);
        case PollCommandKind::DeregisterSource:
            return rstd::move(m_storage.as<PollCommandKind::DeregisterSource>().owner);
        case PollCommandKind::SubmitOperation:
        case PollCommandKind::CancelOperation:
            rstd::panic { "operation command owner belongs to the worker operation slot" };
        case PollCommandKind::ReleaseCompletionSource:
            return rstd::move(m_storage.as<PollCommandKind::ReleaseCompletionSource>().owner);
        case PollCommandKind::ArmTimer:
            return rstd::move(m_storage.as<PollCommandKind::ArmTimer>().owner);
        case PollCommandKind::CancelTimer:
            return rstd::move(m_storage.as<PollCommandKind::CancelTimer>().owner);
        }
        rstd::unreachable();
    }

    auto into_error_event(IoError error) -> PollEvent {
        auto event = [&]() {
            switch (kind()) {
            case PollCommandKind::RegisterSource:
            case PollCommandKind::UpdateInterest:
            case PollCommandKind::DeregisterSource:
                return PollEventData::backend_error(registration_key(), rstd::move(error));
            case PollCommandKind::SubmitOperation:
                if (has_operation_owner()) {
                    return PollEventData::backend_error(operation_key(), rstd::move(error));
                }
                rstd::panic { "reserved operation command error requires its worker slot" };
            case PollCommandKind::CancelOperation:
                rstd::panic { "cancel request failure is not an operation terminal" };
            case PollCommandKind::ArmTimer:
            case PollCommandKind::CancelTimer:
                return PollEventData::backend_error(timer_key(), rstd::move(error));
            case PollCommandKind::ReleaseCompletionSource:
                return PollEventData::source_released(rstd::move(error));
            }
            rstd::unreachable();
        }();
        auto owner =
            kind() == PollCommandKind::SubmitOperation ? take_operation_owner() : take_owner();
        return PollEvent::owned(rstd::move(event), rstd::move(owner));
    }
};

export enum class PollApplyStatus {
    Accepted,
    Rejected,
    Unsupported,
};

export class PollApplyResult {
    enum class Tag : rstd::uint8_t
    {
        Accepted,
        Event,
        Rejected,
        Unsupported,
    };

    struct Failure {
        PollCommand command;
        IoError     error;
    };

    using Storage = Choice<choice_case<Tag::Accepted, void>,
                           choice_case<Tag::Event, PollEvent>,
                           choice_case<Tag::Rejected, Failure>,
                           choice_case<Tag::Unsupported, Failure>>;

    Storage m_storage;

    explicit PollApplyResult(Storage storage): m_storage(rstd::move(storage)) {}

public:
    static auto accepted() -> PollApplyResult {
        return PollApplyResult { Storage::with<Tag::Accepted>() };
    }

    static auto accepted(PollEvent event) -> PollApplyResult {
        return PollApplyResult { Storage::with<Tag::Event>(rstd::move(event)) };
    }

    static auto rejected(PollCommand command, IoError error) -> PollApplyResult {
        return PollApplyResult { Storage::with<Tag::Rejected>(
            Failure { rstd::move(command), rstd::move(error) }) };
    }

    static auto unsupported(PollCommand command) -> PollApplyResult {
        return PollApplyResult { Storage::with<Tag::Unsupported>(Failure {
            rstd::move(command),
            IoError::from_kind(IoErrorKind { IoErrorKind::Unsupported }),
        }) };
    }

    auto status() const noexcept -> PollApplyStatus {
        switch (m_storage.which()) {
        case Tag::Accepted:
        case Tag::Event: return PollApplyStatus::Accepted;
        case Tag::Rejected: return PollApplyStatus::Rejected;
        case Tag::Unsupported: return PollApplyStatus::Unsupported;
        }
        rstd::unreachable();
    }
    auto has_event() const noexcept -> bool { return m_storage.is<Tag::Event>(); }
    auto take_event() -> PollEvent { return rstd::move(m_storage.as<Tag::Event>()); }
    auto take_command() -> PollCommand {
        return m_storage.is<Tag::Rejected>() ? rstd::move(m_storage.as<Tag::Rejected>().command)
                                             : rstd::move(m_storage.as<Tag::Unsupported>().command);
    }
    auto take_error() -> IoError {
        return m_storage.is<Tag::Rejected>() ? rstd::move(m_storage.as<Tag::Rejected>().error)
                                             : rstd::move(m_storage.as<Tag::Unsupported>().error);
    }
};

export class PollBatch {
    Vec<PollEvent> m_events;
    usize          m_cursor {};

public:
    PollBatch(): m_events(Vec<PollEvent>::make()) {}

    auto is_empty() const noexcept -> bool { return m_cursor == m_events.len(); }
    auto len() const noexcept -> usize { return m_events.len() - m_cursor; }
    void push(PollEvent event) { m_events.push(rstd::move(event)); }

    auto pop_front() -> Option<PollEvent> {
        if (is_empty()) return None();
        auto event = rstd::move(m_events[m_cursor]);
        ++m_cursor;
        return Some(rstd::move(event));
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
    RegistrationKey key;
    os::fd::RawFd   fd;
    Interest        interest;
    PollEventOwner  owner;
    rstd::uint8_t   backend_interest_bits {};
    bool            backend_registered { false };

    PollRegistration(RegistrationKey key, os::fd::RawFd fd, Interest interest, PollEventOwner owner)
        : key(key), fd(fd), interest(interest), owner(rstd::move(owner)) {}
};

class RegistrationSlots {
    Vec<Option<PollRegistration>> m_slots;
    usize                         m_len {};

public:
    RegistrationSlots(): m_slots(Vec<Option<PollRegistration>>::make()) {}

    auto get(RegistrationKey key) -> PollRegistration* {
        auto index = key.slot.to_primitive();
        if (index >= m_slots.len().to_primitive() || m_slots[usize(index)].is_none()) {
            return nullptr;
        }
        auto& registration = *m_slots[usize(index)];
        return registration.key == key ? rstd::addressof(registration) : nullptr;
    }

    auto insert(PollRegistration registration) -> bool {
        auto index = registration.key.slot.to_primitive();
        while (m_slots.len().to_primitive() <= index) {
            m_slots.push(None<PollRegistration>());
        }
        if (m_slots[usize(index)].is_some()) return false;
        m_slots[usize(index)] = Some(rstd::move(registration));
        ++m_len;
        return true;
    }

    auto take(RegistrationKey key) -> Option<PollRegistration> {
        if (get(key) == nullptr) return None<PollRegistration>();
        --m_len;
        return m_slots[usize(key.slot.to_primitive())].take();
    }

    auto take_any() -> Option<PollRegistration> {
        for (rstd::size_t i = 0; i < m_slots.len().to_primitive(); ++i) {
            if (m_slots[usize(i)].is_none()) continue;
            --m_len;
            return m_slots[usize(i)].take();
        }
        return None<PollRegistration>();
    }

    auto is_empty() const noexcept -> bool { return m_len == usize(); }
};

struct PollTimer {
    TimerKey       key;
    time::Instant  deadline;
    PollEventOwner owner;

    PollTimer(TimerKey key, time::Instant deadline, PollEventOwner owner)
        : key(key), deadline(deadline), owner(rstd::move(owner)) {}
};

enum class OperationSlotTag : rstd::uint8_t
{
    Free,
    Occupied,
};

struct FreeOperationSlot {
    u32         generation;
    Option<u32> next;
    bool        linked { false };
};

struct OccupiedOperationSlot {
    u32            generation;
    PollEventOwner owner;
    bool           submitted { false };
    bool           cancel_requested { false };
    bool           cancel_issued { false };
    bool           blocking_file { false };
};

using OperationSlot = Choice<choice_case<OperationSlotTag::Free, FreeOperationSlot>,
                             choice_case<OperationSlotTag::Occupied, OccupiedOperationSlot>>;

class OperationSlots {
    Vec<OperationSlot> m_slots;
    Option<u32>        m_free_head {};
    usize              m_occupied {};

    auto slot(OperationKey key) -> OccupiedOperationSlot* {
        auto index = key.slot.to_primitive();
        if (index >= m_slots.len().to_primitive()) return nullptr;
        auto& value = m_slots[usize(index)];
        if (! value.is<OperationSlotTag::Occupied>()) return nullptr;
        auto& occupied = value.as<OperationSlotTag::Occupied>();
        return occupied.generation == key.generation ? rstd::addressof(occupied) : nullptr;
    }

    void release(OperationKey key) {
        auto& occupied   = m_slots[usize(key.slot.to_primitive())].as<OperationSlotTag::Occupied>();
        auto  generation = occupied.generation;
        --m_occupied;
        if (generation == u32::MAX) {
            m_slots[usize(key.slot.to_primitive())].set<OperationSlotTag::Free>(
                FreeOperationSlot { generation, None<u32>(), false });
            return;
        }
        auto next_generation = generation + u32(1);
        m_slots[usize(key.slot.to_primitive())].set<OperationSlotTag::Free>(
            FreeOperationSlot { next_generation, m_free_head, true });
        m_free_head = Some(key.slot);
    }

public:
    OperationSlots(): m_slots(Vec<OperationSlot>::make()) {}

    auto has_free() const noexcept -> bool { return m_free_head.is_some(); }

    auto can_reserve_at(OperationKey key) const noexcept -> bool {
        if (! key.is_valid()) return false;
        auto index = key.slot.to_primitive();
        if (index >= m_slots.len().to_primitive()) return true;
        auto const& value = m_slots[usize(index)];
        if (! value.is<OperationSlotTag::Free>()) return false;
        auto const& free = value.as<OperationSlotTag::Free>();
        return ! free.linked && free.generation == key.generation;
    }

    auto reserve_free(PollEventOwner owner) -> OperationKey {
        if (m_free_head.is_none()) rstd::panic { "operation free list is empty" };
        auto  slot_index = *m_free_head;
        auto& free       = m_slots[usize(slot_index.to_primitive())].as<OperationSlotTag::Free>();
        auto  generation = free.generation;
        m_free_head      = free.next;
        m_slots[usize(slot_index.to_primitive())].set<OperationSlotTag::Occupied>(
            OccupiedOperationSlot { generation, rstd::move(owner) });
        ++m_occupied;
        return OperationKey { slot_index, generation };
    }

    auto reserve_at(OperationKey key, PollEventOwner owner) -> bool {
        auto index = key.slot.to_primitive();
        if (! can_reserve_at(key)) return false;
        while (m_slots.len().to_primitive() < index) {
            m_slots.push(OperationSlot::with<OperationSlotTag::Free>(
                FreeOperationSlot { u32(1), None<u32>(), false }));
        }
        if (index < m_slots.len().to_primitive()) {
            m_slots[usize(index)].set<OperationSlotTag::Occupied>(
                OccupiedOperationSlot { key.generation, rstd::move(owner) });
        } else {
            m_slots.push(OperationSlot::with<OperationSlotTag::Occupied>(
                OccupiedOperationSlot { key.generation, rstd::move(owner) }));
        }
        ++m_occupied;
        return true;
    }

    auto contains(OperationKey key) -> bool { return slot(key) != nullptr; }

    auto mark_submitted(OperationKey key, bool blocking_file = false) -> bool {
        auto* occupied = slot(key);
        if (occupied == nullptr || occupied->submitted) return false;
        occupied->submitted     = true;
        occupied->blocking_file = blocking_file;
        return true;
    }

    void request_cancel(OperationKey key) {
        auto* occupied = slot(key);
        if (occupied != nullptr) occupied->cancel_requested = true;
    }

    auto begin_cancel(OperationKey key) -> Option<bool> {
        auto* occupied = slot(key);
        if (occupied == nullptr || ! occupied->submitted || ! occupied->cancel_requested ||
            occupied->cancel_issued) {
            return None<bool>();
        }
        occupied->cancel_issued = true;
        return Some(occupied->blocking_file);
    }

    auto is_submitted(OperationKey key) -> bool {
        auto* occupied = slot(key);
        return occupied != nullptr && occupied->submitted;
    }

    auto take_owner(OperationKey key) -> Option<PollEventOwner> {
        auto* occupied = slot(key);
        if (occupied == nullptr) return None<PollEventOwner>();
        auto owner = rstd::move(occupied->owner);
        release(key);
        return Some(rstd::move(owner));
    }

    auto is_empty() const noexcept -> bool { return m_occupied == usize(); }
};

export class PollState {
    PollStateKind                        m_kind { PollStateKind::Closed };
    pal_poll::Poller                     m_backend {};
    Option<BlockingFileCompletionDriver> m_blocking_file;
    RegistrationSlots                    m_registrations;
    OperationSlots                       m_operations;
    Vec<PollTimer>                       m_timers;

    explicit PollState(pal_poll::Poller backend, Option<BlockingFileCompletionDriver> blocking_file)
        : m_kind(PollStateKind::Active),
          m_backend(rstd::move(backend)),
          m_blocking_file(rstd::move(blocking_file)),
          m_registrations(),
          m_operations(),
          m_timers(Vec<PollTimer>::make()) {}

    friend class Poll;

public:
    PollState()
        : m_blocking_file(None()),
          m_registrations(),
          m_operations(),
          m_timers(Vec<PollTimer>::make()) {}
    PollState(const PollState&)                        = delete;
    auto operator=(const PollState&) -> PollState&     = delete;
    PollState(PollState&&) noexcept                    = default;
    auto operator=(PollState&&) noexcept -> PollState& = default;

    auto kind() const noexcept -> PollStateKind { return m_kind; }
};

struct PollRuntimeAccess;

export struct PollInit {
    PollState state;
    PollWake  wake;

    PollInit(PollState state, PollWake wake): state(rstd::move(state)), wake(rstd::move(wake)) {}
};

export class Poll {
    friend struct PollRuntimeAccess;

    static void cancel_backend_operation(PollState& state, OperationKey key) {
        auto blocking_file = state.m_operations.begin_cancel(key);
        if (blocking_file.is_none()) return;
        if (*blocking_file) {
            state.m_blocking_file->cancel(key.encode());
        } else {
            (void)state.m_backend.cancel_operation(key.encode());
        }
    }

    static auto backend_interest(Interest interest) noexcept -> pal_poll::Interest {
        return pal_poll::Interest { interest.m_bits.to_primitive() };
    }

    static auto backend_ready(pal_poll::Ready ready) noexcept -> Ready {
        return Ready { u8(ready.bits) };
    }

    static auto backend_operation(OperationKey key, const PollOperation& operation)
        -> pal_poll::Operation {
        auto source_kind = operation.source_kind() == PollSourceKind::Socket
                               ? pal_poll::SourceKind::Socket
                               : pal_poll::SourceKind::File;
        switch (operation.kind()) {
        case PollOperationKind::Read:
            return pal_poll::Operation::read(source_kind,
                                             operation.fd(),
                                             operation.source_key().encode(),
                                             key.encode(),
                                             operation.mutable_data(),
                                             operation.len(),
                                             operation.offset().clone(),
                                             operation.flags());
        case PollOperationKind::Write:
            return pal_poll::Operation::write(source_kind,
                                              operation.fd(),
                                              operation.source_key().encode(),
                                              key.encode(),
                                              operation.const_data(),
                                              operation.len(),
                                              operation.offset().clone(),
                                              operation.flags());
        case PollOperationKind::Connect: {
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
            return pal_poll::Operation::connect(
                operation.fd(), operation.source_key().encode(), key.encode(), address);
        }
        case PollOperationKind::Accept:
            return pal_poll::Operation::accept(operation.fd(),
                                               operation.source_key().encode(),
                                               key.encode(),
                                               operation.secondary_fd(),
                                               operation.mutable_data(),
                                               operation.len());
        }
        rstd::unreachable();
    }

    static auto blocking_file_operation(OperationKey key, const PollOperation& operation)
        -> BlockingFileOperation {
        return BlockingFileOperation {
            operation.kind() == PollOperationKind::Read ? BlockingFileOperationKind::Read
                                                        : BlockingFileOperationKind::Write,
            operation.fd(),
            operation.source_key().encode(),
            key.encode(),
            operation.kind() == PollOperationKind::Read ? operation.mutable_data()
                                                        : const_cast<void*>(operation.const_data()),
            operation.len(),
            operation.offset().clone(),
            operation.flags(),
        };
    }

    static auto find_registration(PollState& state, RegistrationKey key) -> PollRegistration* {
        return state.m_registrations.get(key);
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

        auto updated = registration.backend_registered
                           ? state.m_backend.update_readiness(
                                 registration.key.encode(), registration.fd, backend)
                           : state.m_backend.register_readiness(
                                 registration.key.encode(), registration.fd, backend);
        if (updated.is_err()) {
            return Err(rstd::move(updated).unwrap_err_unchecked());
        }

        registration.interest              = interest;
        registration.backend_interest_bits = backend.bits;
        registration.backend_registered    = true;
        return Ok(empty {});
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
            if (event.kind() == pal_poll::EventKind::Wake) {
                batch.push(PollEvent::wake());
                continue;
            }
            if (event.kind() == pal_poll::EventKind::Readiness) {
                auto  key          = RegistrationKey::decode(event.source_key());
                auto* registration = find_registration(state, key);
                if (registration != nullptr) {
                    auto ready = backend_ready(event.readiness());
                    if (! ready.is_empty()) {
                        batch.push(PollEvent::owned(PollEventData::readiness(key, ready),
                                                    registration->owner.clone()));
                    }
                }
                continue;
            }
            if (event.kind() == pal_poll::EventKind::Completion) {
                auto key   = OperationKey::decode(event.operation_key());
                auto owner = state.m_operations.take_owner(key);
                if (owner.is_none()) continue;
                auto resource = ! event.is_error() && event.resource() != os::fd::INVALID_RAW_FD
                                    ? Some(event.resource())
                                    : None<os::fd::RawFd>();
                auto completion =
                    event.is_error() ? PollCompletion::failure(
                                           IoError::from_raw_os_error(event.error()), event.flags())
                                     : PollCompletion::success(
                                           event.result(), event.flags(), rstd::move(resource));
                batch.push(PollEvent::owned(PollEventData::completion(key, rstd::move(completion)),
                                            rstd::move(owner).unwrap_unchecked()));
            }
        }
    }

    static void append_blocking_file_batch(PollState& state, PollBatch& batch) {
        if (state.m_blocking_file.is_none()) return;
        auto completions = state.m_blocking_file->drain();
        for (auto index = usize(); index < completions.len(); ++index) {
            auto completion = rstd::move(completions[index]);
            auto key        = OperationKey::decode(completion.operation_key);
            auto owner      = state.m_operations.take_owner(key);
            if (owner.is_none()) continue;
            auto result = rstd::move(completion.result);
            auto record = result.is_err()
                              ? PollCompletion::failure(rstd::move(result).unwrap_err_unchecked(),
                                                        completion.flags)
                              : PollCompletion::success(
                                    isize(rstd::move(result).unwrap_unchecked().to_primitive()),
                                    completion.flags);
            batch.push(PollEvent::owned(PollEventData::completion(key, rstd::move(record)),
                                        rstd::move(owner).unwrap_unchecked()));
        }
    }

    static auto initialize(IoBackendPreference preference, Option<BlockingSpawner> blocking_spawner)
        -> io::Result<PollInit> {
        auto backend_preference = pal_poll::BackendPreference::Auto;
        if (preference == IoBackendPreference::NativeCompletionRequired) {
            backend_preference = pal_poll::BackendPreference::NativeCompletionRequired;
        } else if (preference == IoBackendPreference::ReadinessEmulationRequired) {
            backend_preference = pal_poll::BackendPreference::ReadinessEmulationRequired;
        }
        auto initialized = pal_poll::Poller::init(backend_preference);
        if (initialized.is_err()) {
            return Err(rstd::move(initialized).unwrap_err_unchecked());
        }
        auto backend       = rstd::move(initialized).unwrap_unchecked();
        auto blocking_file = Option<BlockingFileCompletionDriver> {};
#if RSTD_OS_LINUX
        if (! backend.poller.capabilities().contains(pal_poll::Capability::FileCompletion) &&
            blocking_spawner.is_some()) {
            blocking_file = Some(BlockingFileCompletionDriver {
                rstd::move(blocking_spawner).unwrap_unchecked(), backend.wake.clone() });
        }
#else
        (void)blocking_spawner;
#endif
        return Ok(PollInit { PollState { rstd::move(backend.poller), rstd::move(blocking_file) },
                             PollWake { rstd::move(backend.wake) } });
    }

public:
    static auto init(IoBackendPreference preference = IoBackendPreference::Auto)
        -> io::Result<PollInit> {
        return initialize(preference, None<BlockingSpawner>());
    }

    static auto capabilities(const PollState& state) noexcept -> PollCapabilities {
        auto backend = state.m_backend.capabilities();
        auto result  = PollCapabilities::none();
        if (backend.contains(pal_poll::Capability::Readiness)) {
            result = result | PollCapability::Readiness;
        }
        if (backend.contains(pal_poll::Capability::SocketCompletion)) {
            result = result | PollCapability::SocketCompletion;
        }
        if (backend.contains(pal_poll::Capability::FileCompletion)) {
            result = result | PollCapability::FileCompletion;
        }
        if (state.m_blocking_file.is_some()) {
            result = result | PollCapability::FileCompletion;
        }
        if (backend.contains(pal_poll::Capability::Timer)) {
            result = result | PollCapability::Timer;
        }
        if (backend.contains(pal_poll::Capability::Wake)) {
            result = result | PollCapability::Wake;
        }
        return result;
    }

    static auto has_recycled_operation(const PollState& state) noexcept -> bool {
        return state.m_kind == PollStateKind::Active && state.m_operations.has_free();
    }

    static auto reserve_recycled_operation(PollState& state, PollEventOwner owner) -> OperationKey {
        if (state.m_kind != PollStateKind::Active) {
            rstd::panic { "operation reserved while async Poll is not active" };
        }
        return state.m_operations.reserve_free(rstd::move(owner));
    }

    static auto reserve_fresh_operation(PollState& state, OperationKey key, PollEventOwner owner)
        -> bool {
        return state.m_kind == PollStateKind::Active &&
               state.m_operations.reserve_at(key, rstd::move(owner));
    }

    static auto abandon_operation(PollState& state, OperationKey key) -> Option<PollEventOwner> {
        if (state.m_operations.is_submitted(key)) return None<PollEventOwner>();
        return state.m_operations.take_owner(key);
    }

    static auto apply(PollState& state, PollCommand command) -> PollApplyResult {
        if (state.m_kind == PollStateKind::Closed) {
            return PollApplyResult::rejected(
                rstd::move(command), IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
        }

        switch (command.kind()) {
        case PollCommandKind::RegisterSource: {
            if (! command.registration_key().is_valid() ||
                find_registration(state, command.registration_key()) != nullptr) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
            }

            auto registration = PollRegistration {
                command.registration_key(), command.fd(), Interest {}, command.owner().clone()
            };
            auto updated = update_registration(state, registration, command.interest());
            if (updated.is_err()) {
                return PollApplyResult::rejected(rstd::move(command),
                                                 rstd::move(updated).unwrap_err_unchecked());
            }
            if (! state.m_registrations.insert(rstd::move(registration))) {
                rstd::panic { "validated registration slot could not be installed" };
            }
            return PollApplyResult::accepted();
        }
        case PollCommandKind::UpdateInterest: {
            if (! command.registration_key().is_valid()) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
            }
            auto* registration = find_registration(state, command.registration_key());
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
            if (! command.registration_key().is_valid()) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
            }
            auto* registration = state.m_registrations.get(command.registration_key());
            if (registration != nullptr) {
                auto updated = update_registration(state, *registration, Interest {});
                if (updated.is_err()) {
                    return PollApplyResult::rejected(rstd::move(command),
                                                     rstd::move(updated).unwrap_err_unchecked());
                }
                (void)state.m_registrations.take(command.registration_key());
                return PollApplyResult::accepted(PollEvent::owned(
                    PollEventData::deregistered(command.registration_key()), command.take_owner()));
            }
            return PollApplyResult::accepted(PollEvent::owned(
                PollEventData::deregistered(command.registration_key()), command.take_owner()));
        }
        case PollCommandKind::SubmitOperation: {
            auto key = command.operation_key();
            if (! state.m_operations.contains(key) && command.has_operation_owner()) {
                auto owner = command.take_operation_owner();
                if (! state.m_operations.can_reserve_at(key)) {
                    return PollApplyResult::accepted(PollEvent::owned(
                        PollEventData::backend_error(
                            key, IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput })),
                        rstd::move(owner)));
                }
                (void)state.m_operations.reserve_at(key, rstd::move(owner));
            }
            if (! key.is_valid() || ! state.m_operations.contains(key) ||
                state.m_operations.is_submitted(key)) {
                return PollApplyResult::accepted();
            }
            auto use_blocking_file = command.operation().source_kind() == PollSourceKind::File &&
                                     state.m_blocking_file.is_some();
            auto submitted =
                use_blocking_file
                    ? state.m_blocking_file->submit(
                          blocking_file_operation(key, command.operation()))
                    : state.m_backend.submit_operation(backend_operation(key, command.operation()));
            if (submitted.is_err()) {
                auto error = rstd::move(submitted).unwrap_err_unchecked();
                auto owner = state.m_operations.take_owner(key);
                if (owner.is_none()) return PollApplyResult::accepted();
                return PollApplyResult::accepted(
                    PollEvent::owned(PollEventData::backend_error(key, rstd::move(error)),
                                     rstd::move(owner).unwrap_unchecked()));
            }
            if (! state.m_operations.mark_submitted(key, use_blocking_file)) {
                rstd::panic { "accepted backend operation lost its owner slot" };
            }
            cancel_backend_operation(state, key);
            return PollApplyResult::accepted();
        }
        case PollCommandKind::CancelOperation: {
            auto key = command.operation_key();
            if (! key.is_valid()) return PollApplyResult::accepted();
            state.m_operations.request_cancel(key);
            cancel_backend_operation(state, key);
            return PollApplyResult::accepted();
        }
        case PollCommandKind::ReleaseCompletionSource: {
            if (! command.source_key().is_valid()) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
            }
            if (state.m_blocking_file.is_some() &&
                state.m_blocking_file->source_active(command.source_key().encode())) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::ResourceBusy }));
            }
            auto released = state.m_backend.release_completion_source(command.source_key().encode(),
                                                                      command.fd());
            if (released.is_err()) {
                return PollApplyResult::rejected(rstd::move(command),
                                                 rstd::move(released).unwrap_err_unchecked());
            }
            return PollApplyResult::accepted(
                PollEvent::owned(PollEventData::source_released(), command.take_owner()));
        }
        case PollCommandKind::ArmTimer: {
            if (! command.timer_key().is_valid()) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
            }
            for (rstd::size_t i = 0; i < state.m_timers.len().to_primitive(); ++i) {
                if (state.m_timers[usize(i)].key == command.timer_key()) {
                    return PollApplyResult::rejected(
                        rstd::move(command),
                        IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
                }
            }

            state.m_timers.push(
                PollTimer { command.timer_key(), command.deadline(), command.owner().clone() });
            return PollApplyResult::accepted();
        }
        case PollCommandKind::CancelTimer:
            if (! command.timer_key().is_valid()) {
                return PollApplyResult::rejected(
                    rstd::move(command),
                    IoError::from_kind(IoErrorKind { IoErrorKind::InvalidInput }));
            }
            for (rstd::size_t i = 0; i < state.m_timers.len().to_primitive(); ++i) {
                auto index = usize(i);
                if (state.m_timers[index].key != command.timer_key()) continue;
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

        auto batch = PollBatch {};
        append_blocking_file_batch(state, batch);
        if (! batch.is_empty()) {
            auto collected = collect_expired_timers(state, batch);
            if (collected.is_err()) {
                return Err(rstd::move(collected).unwrap_err_unchecked());
            }
            return Ok(rstd::move(batch));
        }

        state.m_kind = PollStateKind::Waiting;
        auto waited =
            state.m_backend.wait(timeout == PollTimeout::Immediate ? pal_poll::WaitMode::Immediate
                                                                   : pal_poll::WaitMode::Infinite,
                                 next_timer_duration(state));
        state.m_kind = PollStateKind::Active;
        if (waited.is_err()) return Err(rstd::move(waited).unwrap_err_unchecked());

        append_backend_batch(state, rstd::move(waited).unwrap_unchecked(), batch);
        append_blocking_file_batch(state, batch);
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
        if (state.m_blocking_file.is_some()) {
            state.m_blocking_file->cancel_all();
            append_blocking_file_batch(state, batch);
        }
        while (! state.m_operations.is_empty()) {
            auto waited =
                state.m_backend.wait(pal_poll::WaitMode::Infinite, None<time::Duration>());
            if (waited.is_err()) {
                rstd::panic { "async Poll backend failed while draining operations" };
            }
            append_backend_batch(state, rstd::move(waited).unwrap_unchecked(), batch);
            append_blocking_file_batch(state, batch);
        }
        while (! state.m_registrations.is_empty()) {
            auto registration = state.m_registrations.take_any().unwrap_unchecked();
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
        state.m_blocking_file = None<BlockingFileCompletionDriver>();
        state.m_backend       = pal_poll::Poller {};
        state.m_kind          = PollStateKind::Closed;
        return batch;
    }
};

struct PollRuntimeAccess {
    static auto init(IoBackendPreference preference, BlockingSpawner blocking_spawner)
        -> io::Result<PollInit> {
        return Poll::initialize(preference, Some(rstd::move(blocking_spawner)));
    }
};

} // namespace rstd::async

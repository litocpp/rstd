module;
#include <rstd/macro.hpp>

export module rstd:sys.pal.poll.types;
export import :io.error;
export import :os.fd;
export import rstd.core;

namespace rstd::sys::pal::poll
{

export enum class Capability : rstd::uint8_t {
    Readiness  = 1,
    Completion = 2,
    Timer      = 4,
    Wake       = 8,
};

export class Capabilities {
    rstd::uint8_t m_bits {};

    explicit constexpr Capabilities(rstd::uint8_t bits) noexcept: m_bits(bits) {}

public:
    constexpr Capabilities() noexcept = default;

    static constexpr auto none() noexcept -> Capabilities { return Capabilities {}; }

    static constexpr auto of(Capability capability) noexcept -> Capabilities {
        return Capabilities { static_cast<rstd::uint8_t>(capability) };
    }

    constexpr auto contains(Capability capability) const noexcept -> bool {
        auto bit = static_cast<rstd::uint8_t>(capability);
        return (m_bits & bit) == bit;
    }

    friend constexpr auto operator|(Capabilities capabilities, Capability capability) noexcept
        -> Capabilities {
        return Capabilities { static_cast<rstd::uint8_t>(capabilities.m_bits |
                                                         static_cast<rstd::uint8_t>(capability)) };
    }
};

export struct Interest {
    rstd::uint8_t bits {};

    static constexpr rstd::uint8_t READABLE = 1;
    static constexpr rstd::uint8_t WRITABLE = 2;

    constexpr auto is_readable() const noexcept -> bool { return (bits & READABLE) != 0; }
    constexpr auto is_writable() const noexcept -> bool { return (bits & WRITABLE) != 0; }
    constexpr auto is_empty() const noexcept -> bool { return bits == 0; }
};

export struct Ready {
    rstd::uint8_t bits {};

    static constexpr rstd::uint8_t READABLE     = 1;
    static constexpr rstd::uint8_t WRITABLE     = 2;
    static constexpr rstd::uint8_t READ_CLOSED  = 4;
    static constexpr rstd::uint8_t WRITE_CLOSED = 8;
    static constexpr rstd::uint8_t ERROR        = 16;

    constexpr auto is_empty() const noexcept -> bool { return bits == 0; }
};

export enum class EventKind {
    Wake,
    Readiness,
    Completion,
    SourceError,
};

export enum class OperationKind {
    Read,
    Write,
    Connect,
    Accept,
};

export enum class SourceKind {
    File,
    Socket,
};

export enum class WaitMode {
    Immediate,
    Infinite,
};

export struct SocketAddress {
    bool ipv6 {};
    u8   octets[16] {};
    u16  port {};
    u32  flowinfo {};
    u32  scope_id {};
};

export struct Operation {
    OperationKind kind { OperationKind::Read };
    SourceKind    source_kind { SourceKind::File };
    os::fd::RawFd handle { os::fd::INVALID_RAW_FD };
    os::fd::RawFd secondary_handle { os::fd::INVALID_RAW_FD };
    u64           source_key {};
    u64           operation_key {};
    void*         mutable_data { nullptr };
    const void*   const_data { nullptr };
    usize         len {};
    Option<u64>   offset {};
    u32           flags {};
    SocketAddress address {};
    bool          started {};
};

export struct Event {
    EventKind                     kind { EventKind::Wake };
    u64                           source_key {};
    u64                           operation_key {};
    Ready                         ready {};
    isize                         result {};
    u32                           flags {};
    os::fd::RawFd                 resource { os::fd::INVALID_RAW_FD };
    Option<io::error::RawOsError> error {};

    static auto wake() noexcept -> Event { return Event {}; }

    static auto readiness(u64 source_key, Ready ready) noexcept -> Event {
        auto event       = Event {};
        event.kind       = EventKind::Readiness;
        event.source_key = source_key;
        event.ready      = ready;
        return event;
    }

    static auto completion(u64 operation_key, isize result, u32 flags = u32()) noexcept -> Event {
        auto event          = Event {};
        event.kind          = EventKind::Completion;
        event.operation_key = operation_key;
        event.result        = result;
        event.flags         = flags;
        return event;
    }

    static auto completion_resource(u64           operation_key,
                                    os::fd::RawFd resource,
                                    u32           flags = u32()) noexcept -> Event {
        auto event     = completion(operation_key, isize(), flags);
        event.resource = resource;
        return event;
    }

    static auto completion_error(u64                   operation_key,
                                 io::error::RawOsError error,
                                 u32                   flags = u32()) noexcept -> Event {
        auto event          = Event {};
        event.kind          = EventKind::Completion;
        event.operation_key = operation_key;
        event.flags         = flags;
        event.error         = Some(error);
        return event;
    }
};

export class Batch {
    static constexpr rstd::size_t CAPACITY = 64;

    Event        m_events[CAPACITY] {};
    rstd::size_t m_len {};

public:
    static constexpr auto capacity() noexcept -> rstd::size_t { return CAPACITY; }

    auto is_empty() const noexcept -> bool { return m_len == 0; }
    auto len() const noexcept -> usize { return usize(m_len); }

    void push(Event event) {
        debug_assert(m_len < CAPACITY);
        m_events[m_len++] = rstd::move(event);
    }

    auto operator[](usize index) noexcept -> Event& { return m_events[index.to_primitive()]; }

    auto operator[](usize index) const noexcept -> const Event& {
        return m_events[index.to_primitive()];
    }
};

} // namespace rstd::sys::pal::poll

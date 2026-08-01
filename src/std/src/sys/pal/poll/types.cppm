module;
#include <rstd/macro.hpp>

export module rstd:sys.pal.poll.types;
export import :io.error;
export import :os.fd;
export import rstd.core;

namespace rstd::sys::pal::poll
{

export enum class Capability : rstd::uint8_t {
    Readiness        = 1,
    SocketCompletion = 2,
    FileCompletion   = 4,
    Completion       = SocketCompletion | FileCompletion,
    Timer            = 8,
    Wake             = 16,
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

export enum class BackendPreference {
    Auto,
    NativeCompletionRequired,
    ReadinessEmulationRequired,
};

export struct SocketAddress {
    bool ipv6 {};
    u8   octets[16] {};
    u16  port {};
    u32  flowinfo {};
    u32  scope_id {};
};

export class Operation {
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
        SocketAddress address;
    };

    struct Accept {
        os::fd::RawFd secondary_handle;
        void*         address_buffer;
        usize         address_buffer_len;
    };

    using Storage = Choice<choice_case<OperationKind::Read, Read>,
                           choice_case<OperationKind::Write, Write>,
                           choice_case<OperationKind::Connect, Connect>,
                           choice_case<OperationKind::Accept, Accept>>;

    SourceKind    m_source_kind;
    os::fd::RawFd m_handle;
    u64           m_source_key;
    u64           m_operation_key;
    Storage       m_storage;

    Operation(SourceKind    source_kind,
              os::fd::RawFd handle,
              u64           source_key,
              u64           operation_key,
              Storage       storage)
        : m_source_kind(source_kind),
          m_handle(handle),
          m_source_key(source_key),
          m_operation_key(operation_key),
          m_storage(rstd::move(storage)) {}

public:
    static auto read(SourceKind    source_kind,
                     os::fd::RawFd handle,
                     u64           source_key,
                     u64           operation_key,
                     void*         data,
                     usize         len,
                     Option<u64>   offset,
                     u32           flags) -> Operation {
        return Operation { source_kind,
                           handle,
                           source_key,
                           operation_key,
                           Storage::with<OperationKind::Read>(
                               Read { data, len, rstd::move(offset), flags }) };
    }

    static auto write(SourceKind    source_kind,
                      os::fd::RawFd handle,
                      u64           source_key,
                      u64           operation_key,
                      const void*   data,
                      usize         len,
                      Option<u64>   offset,
                      u32           flags) -> Operation {
        return Operation { source_kind,
                           handle,
                           source_key,
                           operation_key,
                           Storage::with<OperationKind::Write>(
                               Write { data, len, rstd::move(offset), flags }) };
    }

    static auto
    connect(os::fd::RawFd handle, u64 source_key, u64 operation_key, SocketAddress address)
        -> Operation {
        return Operation { SourceKind::Socket,
                           handle,
                           source_key,
                           operation_key,
                           Storage::with<OperationKind::Connect>(Connect { address }) };
    }

    static auto accept(os::fd::RawFd handle,
                       u64           source_key,
                       u64           operation_key,
                       os::fd::RawFd secondary_handle,
                       void*         address_buffer,
                       usize         address_buffer_len) -> Operation {
        return Operation { SourceKind::Socket,
                           handle,
                           source_key,
                           operation_key,
                           Storage::with<OperationKind::Accept>(
                               Accept { secondary_handle, address_buffer, address_buffer_len }) };
    }

    auto kind() const noexcept -> OperationKind { return m_storage.which(); }
    auto source_kind() const noexcept -> SourceKind { return m_source_kind; }
    auto handle() const noexcept -> os::fd::RawFd { return m_handle; }
    auto source_key() const noexcept -> u64 { return m_source_key; }
    auto operation_key() const noexcept -> u64 { return m_operation_key; }
    auto secondary_handle() const noexcept -> os::fd::RawFd {
        return m_storage.as<OperationKind::Accept>().secondary_handle;
    }
    auto mutable_data() const noexcept -> void* {
        return kind() == OperationKind::Read ? m_storage.as<OperationKind::Read>().data
                                             : m_storage.as<OperationKind::Accept>().address_buffer;
    }
    auto const_data() const noexcept -> const void* {
        return m_storage.as<OperationKind::Write>().data;
    }
    auto len() const noexcept -> usize {
        switch (kind()) {
        case OperationKind::Read: return m_storage.as<OperationKind::Read>().len;
        case OperationKind::Write: return m_storage.as<OperationKind::Write>().len;
        case OperationKind::Accept: return m_storage.as<OperationKind::Accept>().address_buffer_len;
        case OperationKind::Connect: return usize();
        }
        rstd::unreachable();
    }
    auto offset() const noexcept -> const Option<u64>& {
        return kind() == OperationKind::Read ? m_storage.as<OperationKind::Read>().offset
                                             : m_storage.as<OperationKind::Write>().offset;
    }
    auto flags() const noexcept -> u32 {
        switch (kind()) {
        case OperationKind::Read: return m_storage.as<OperationKind::Read>().flags;
        case OperationKind::Write: return m_storage.as<OperationKind::Write>().flags;
        case OperationKind::Connect:
        case OperationKind::Accept: return u32();
        }
        rstd::unreachable();
    }
    auto address() const noexcept -> SocketAddress {
        return m_storage.as<OperationKind::Connect>().address;
    }
};

export class Event {
    enum class CompletionTag : rstd::uint8_t
    {
        Success,
        Failure,
    };

    struct Readiness {
        u64   source_key;
        Ready ready;
    };

    struct CompletionSuccess {
        isize         result;
        u32           flags;
        os::fd::RawFd resource;
    };

    struct CompletionFailure {
        io::error::RawOsError error;
        u32                   flags;
    };

    using CompletionStorage = Choice<choice_case<CompletionTag::Success, CompletionSuccess>,
                                     choice_case<CompletionTag::Failure, CompletionFailure>>;

    struct Completion {
        u64               operation_key;
        CompletionStorage result;
    };

    struct SourceError {
        u64                   source_key;
        io::error::RawOsError error;
    };

    using Storage = Choice<choice_case<EventKind::Wake, void>,
                           choice_case<EventKind::Readiness, Readiness>,
                           choice_case<EventKind::Completion, Completion>,
                           choice_case<EventKind::SourceError, SourceError>>;

    Storage m_storage;

    explicit Event(Storage storage): m_storage(rstd::move(storage)) {}

public:
    static auto wake() noexcept -> Event { return Event { Storage::with<EventKind::Wake>() }; }

    static auto readiness(u64 source_key, Ready ready) noexcept -> Event {
        return Event { Storage::with<EventKind::Readiness>(Readiness { source_key, ready }) };
    }

    static auto completion(u64 operation_key, isize result, u32 flags = u32()) noexcept -> Event {
        return Event { Storage::with<EventKind::Completion>(Completion {
            operation_key,
            CompletionStorage::with<CompletionTag::Success>(CompletionSuccess {
                result,
                flags,
                os::fd::INVALID_RAW_FD,
            }),
        }) };
    }

    static auto completion_resource(u64           operation_key,
                                    os::fd::RawFd resource,
                                    u32           flags = u32()) noexcept -> Event {
        return Event { Storage::with<EventKind::Completion>(Completion {
            operation_key,
            CompletionStorage::with<CompletionTag::Success>(
                CompletionSuccess { isize(), flags, resource }),
        }) };
    }

    static auto completion_error(u64                   operation_key,
                                 io::error::RawOsError error,
                                 u32                   flags = u32()) noexcept -> Event {
        return Event { Storage::with<EventKind::Completion>(Completion {
            operation_key,
            CompletionStorage::with<CompletionTag::Failure>(CompletionFailure { error, flags }),
        }) };
    }

    static auto source_error(u64 source_key, io::error::RawOsError error) noexcept -> Event {
        return Event { Storage::with<EventKind::SourceError>(SourceError { source_key, error }) };
    }

    auto kind() const noexcept -> EventKind { return m_storage.which(); }
    auto source_key() const noexcept -> u64 {
        return kind() == EventKind::Readiness ? m_storage.as<EventKind::Readiness>().source_key
                                              : m_storage.as<EventKind::SourceError>().source_key;
    }
    auto readiness() const noexcept -> Ready { return m_storage.as<EventKind::Readiness>().ready; }
    auto operation_key() const noexcept -> u64 {
        return m_storage.as<EventKind::Completion>().operation_key;
    }
    auto is_error() const noexcept -> bool {
        return m_storage.as<EventKind::Completion>().result.is<CompletionTag::Failure>();
    }
    auto result() const noexcept -> isize {
        return m_storage.as<EventKind::Completion>().result.as<CompletionTag::Success>().result;
    }
    auto flags() const noexcept -> u32 {
        auto& result = m_storage.as<EventKind::Completion>().result;
        return result.is<CompletionTag::Success>() ? result.as<CompletionTag::Success>().flags
                                                   : result.as<CompletionTag::Failure>().flags;
    }
    auto resource() const noexcept -> os::fd::RawFd {
        return m_storage.as<EventKind::Completion>().result.as<CompletionTag::Success>().resource;
    }
    auto error() const noexcept -> io::error::RawOsError {
        return kind() == EventKind::Completion
                   ? m_storage.as<EventKind::Completion>().result.as<CompletionTag::Failure>().error
                   : m_storage.as<EventKind::SourceError>().error;
    }
};

export class Batch {
    static constexpr rstd::size_t CAPACITY = 64;

    mem::MaybeUninit<Event> m_events[CAPACITY];
    rstd::size_t            m_len {};

public:
    Batch() noexcept                       = default;
    Batch(const Batch&)                    = delete;
    auto operator=(const Batch&) -> Batch& = delete;

    Batch(Batch&& other) {
        for (rstd::size_t i = 0; i < other.m_len; ++i) {
            m_events[i].write(rstd::move(other.m_events[i].assume_init_mut()));
            other.m_events[i].assume_init_drop();
        }
        m_len       = other.m_len;
        other.m_len = 0;
    }

    auto operator=(Batch&& other) -> Batch& {
        if (this == rstd::addressof(other)) return *this;
        clear();
        for (rstd::size_t i = 0; i < other.m_len; ++i) {
            m_events[i].write(rstd::move(other.m_events[i].assume_init_mut()));
            other.m_events[i].assume_init_drop();
        }
        m_len       = other.m_len;
        other.m_len = 0;
        return *this;
    }

    ~Batch() { clear(); }

    static constexpr auto capacity() noexcept -> rstd::size_t { return CAPACITY; }

    auto is_empty() const noexcept -> bool { return m_len == 0; }
    auto len() const noexcept -> usize { return usize(m_len); }

    void push(Event event) {
        debug_assert(m_len < CAPACITY);
        m_events[m_len++].write(rstd::move(event));
    }

    auto operator[](usize index) noexcept -> Event& {
        return m_events[index.to_primitive()].assume_init_mut();
    }

    auto operator[](usize index) const noexcept -> const Event& {
        return m_events[index.to_primitive()].assume_init_ref();
    }

    void clear() noexcept {
        while (m_len != 0) {
            m_events[--m_len].assume_init_drop();
        }
    }
};

} // namespace rstd::sys::pal::poll

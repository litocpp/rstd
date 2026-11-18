module;
#include <rstd/macro.hpp>

export module rstd:async.io_operation;
export import :async.forward;
export import :bytes;
export import rstd.io.error;
import :async.awaitable;
#if LITO_FEAT_ASYNC_DEBUG
import :async.debug;
#endif
import :async.poll;
import :async.runtime_core;
import rstd.net.socket_addr;
import rstd.os.fd;
import rstd.os.handle;
import rstd.os.socket;
import rstd.sys.socket;
export import rstd.net.datagram;
import :sync;
import rstd.alloc;

using namespace rstd;

namespace rstd::async
{

export enum class CompletionSourceKind {
    File,
    Socket,
};

struct IoOperationState;
struct CompletionSourceState;

using CompletionSourceArc = sync::Arc<CompletionSourceState>;

struct CompletionSourceBinding {
    WorkerHandle worker;
    SourceKey    source_key {};
};

struct CompletionSourceFields {
#if LITO_FEAT_ASYNC_DEBUG
    debug::Recorder diagnostic;
#endif
    Option<os::fd::OwnedFd>         owned_file {};
    Option<os::socket::OwnedSocket> owned_socket {};
    Option<CompletionSourceBinding> binding {};
    usize                           active_operations {};
    Option<FacilityCompletionToken> release_token {};
    Option<task::Waker>             release_waker {};
    Option<io::error::Error>        release_error {};
    bool                            release_requested { false };
    bool                            release_submitted { false };
    bool                            released { false };
};

struct CompletionSourceState {
    os::fd::RawFd                       fd { os::fd::INVALID_RAW_FD };
    CompletionSourceKind                kind { CompletionSourceKind::File };
    sync::Mutex<CompletionSourceFields> fields;

    CompletionSourceState(os::fd::RawFd fd, CompletionSourceKind kind);

    explicit CompletionSourceState(os::fd::OwnedFd file);

    explicit CompletionSourceState(os::socket::OwnedSocket socket);

    ~CompletionSourceState();
};

export class CompletionSourceRelease;

export class CompletionSource {
    CompletionSourceArc m_state;

    CompletionSource(os::fd::RawFd fd, CompletionSourceKind kind);

    explicit CompletionSource(CompletionSourceArc state): m_state(rstd::move(state)) {}

    auto bind_worker(WorkerHandle current) const -> io::Result<WorkerHandle>;

    auto source_key() const -> SourceKey;

    auto begin_operation() const -> bool;

    friend struct IoOperationState;

public:
    CompletionSource(const CompletionSource&)                        = delete;
    auto operator=(const CompletionSource&) -> CompletionSource&     = delete;
    CompletionSource(CompletionSource&&) noexcept                    = default;
    auto operator=(CompletionSource&&) noexcept -> CompletionSource& = default;

    static auto file(os::fd::BorrowedFd fd) noexcept -> CompletionSource;

    // In-flight operations and backend release retain ownership after the caller drops the source.
    static auto file(os::fd::OwnedFd fd) -> CompletionSource;

#if RSTD_OS_WINDOWS
    static auto file(os::handle::BorrowedHandle handle) noexcept -> CompletionSource;
    static auto file(os::handle::OwnedHandle handle) -> CompletionSource;
#endif

    static auto socket(os::fd::BorrowedFd fd) noexcept -> CompletionSource;

    static auto socket(os::socket::BorrowedSocket socket) noexcept -> CompletionSource;

    static auto socket(os::socket::OwnedSocket socket) -> CompletionSource;
    auto take_owned_socket() -> io::Result<os::socket::OwnedSocket>;
    auto clone() const -> CompletionSource { return CompletionSource { m_state.clone() }; }
    auto as_raw_fd() const noexcept -> os::fd::RawFd { return m_state->fd; }
    auto kind() const noexcept -> CompletionSourceKind { return m_state->kind; }
    auto is_bound() const -> bool;
    auto release() && -> CompletionSourceRelease;
};

export class IoCompletion {
    usize                           m_transferred {};
    u32                             m_flags {};
    bytes::Bytes                    m_data;
    Option<os::socket::OwnedSocket> m_socket {};
    Option<net::DatagramInfo>       m_datagram {};

public:
    IoCompletion(usize                           transferred,
                 u32                             flags,
                 bytes::Bytes                    data,
                 Option<os::socket::OwnedSocket> socket   = None(),
                 Option<net::DatagramInfo>       datagram = None());

    auto transferred() const noexcept -> usize { return m_transferred; }
    auto flags() const noexcept -> u32 { return m_flags; }
    auto data() const noexcept [[clang::lifetimebound]] -> slice<u8> { return m_data.as_slice(); }
    auto into_data() && -> bytes::Bytes { return rstd::move(m_data); }
    auto has_socket() const noexcept -> bool { return m_socket.is_some(); }
    auto datagram() const noexcept [[clang::lifetimebound]] -> const Option<net::DatagramInfo>& {
        return m_datagram;
    }
    auto into_socket() && -> Option<os::socket::OwnedSocket> { return m_socket.take(); }
};

enum class IoOperationLifecycle
{
    Created,
    Submitted,
    Ready,
    Consumed,
};

enum class IoOperationConsumer
{
    None,
    Direct,
    Poll,
};

struct IoOperationFields {
#if LITO_FEAT_ASYNC_DEBUG
    debug::Recorder diagnostic;
#endif
    Option<WorkerHandle>             worker {};
    Option<OperationKey>             key {};
    Option<FacilityCompletionToken>  token {};
    Option<task::Waker>              waker {};
    Option<io::Result<IoCompletion>> result {};
    IoOperationLifecycle             lifecycle { IoOperationLifecycle::Created };
    IoOperationConsumer              consumer { IoOperationConsumer::None };
    bool                             cancel_requested { false };
    bool                             source_active { true };

    void set_ready(io::Result<IoCompletion> output);

#if LITO_FEAT_ASYNC_DEBUG
    void publish_ready() const;
#endif
};

struct IoOperationState {
    struct Read {
        bytes::BytesMut buffer;
        Option<u64>     offset;
        usize           requested;
        bool            return_buffer { false };
    };

    struct Write {
        bytes::Bytes buffer;
    };

    struct Connect {
        net::SocketAddr address;
    };

    struct Accept {
        net::SocketAddr                 address;
        Option<os::socket::OwnedSocket> socket;
        bytes::BytesMut                 address_buffer;
    };

    struct RecvMessage {
        bytes::BytesMut                                   buffer;
        ::alloc::boxed::Box<sys::socket::DatagramMessage> message;
        bool                                              return_buffer { false };
    };
    struct SendMessage {
        bytes::Bytes                                      buffer;
        ::alloc::boxed::Box<sys::socket::DatagramMessage> message;
    };

    using Payload = Choice<choice_case<PollOperationKind::Read, Read>,
                           choice_case<PollOperationKind::Write, Write>,
                           choice_case<PollOperationKind::Connect, Connect>,
                           choice_case<PollOperationKind::Accept, Accept>,
                           choice_case<PollOperationKind::RecvMessage, RecvMessage>,
                           choice_case<PollOperationKind::SendMessage, SendMessage>>;

#if LITO_FEAT_ASYNC_DEBUG
    struct ReleaseObservation {
        debug::Recorder record;
        ~ReleaseObservation();
    };
#endif
    CompletionSource source;
#if LITO_FEAT_ASYNC_DEBUG
    ReleaseObservation release_observation;
#endif
    Payload                        payload;
    sync::Mutex<IoOperationFields> fields;

    IoOperationState(CompletionSource source, usize read_len, Option<u64> offset = None());

    IoOperationState(CompletionSource source, bytes::Bytes data);

    IoOperationState(CompletionSource source, bytes::BytesMut buffer);

    IoOperationState(CompletionSource source, net::SocketAddr address);

    IoOperationState(CompletionSource                source,
                     net::SocketAddr                 address,
                     Option<os::socket::OwnedSocket> accepted_socket,
                     bytes::BytesMut                 accept_address_buffer);

    IoOperationState(CompletionSource                                  source,
                     bytes::BytesMut                                   buffer,
                     ::alloc::boxed::Box<sys::socket::DatagramMessage> message);
    IoOperationState(CompletionSource                                  source,
                     bytes::Bytes                                      buffer,
                     ::alloc::boxed::Box<sys::socket::DatagramMessage> message);

    void begin();

    ~IoOperationState();

    auto poll_operation() -> PollOperation;

    auto finish(PollCompletion completion) -> io::Result<IoCompletion>;

    auto finish(PollEventData event) -> io::Result<IoCompletion>;

    auto required_capability() const noexcept -> PollCapability;

    auto bind_worker(WorkerHandle current) const -> io::Result<WorkerHandle>;

#if LITO_FEAT_ASYNC_DEBUG
    void observe(IoOperationFields& fields);
#endif

    auto source_state() const -> CompletionSourceArc;
};

using IoOperationArc = sync::Arc<IoOperationState>;

export class CompletionSourceRelease {
#if LITO_FEAT_ASYNC_DEBUG
    debug::WaitGuard debug_wait_;
    void observe(CompletionSourceFields& fields);
#endif
    CompletionSourceArc m_state;
    bool                m_completed { false };
    bool                m_polling { false };

    explicit CompletionSourceRelease(CompletionSourceArc state);

    friend class CompletionSource;

public:
    using Output = io::Result<empty>;

    CompletionSourceRelease(const CompletionSourceRelease&)                    = delete;
    auto operator=(const CompletionSourceRelease&) -> CompletionSourceRelease& = delete;
    CompletionSourceRelease(CompletionSourceRelease&& other) noexcept;
    auto operator=(CompletionSourceRelease&& other) noexcept -> CompletionSourceRelease&;
    ~CompletionSourceRelease();

    auto advance(AwaitContext& cx) -> AwaitTransition;

    auto submit_completion(FacilityCompletionToken token) -> FacilityCompletionSubmitResult;

    auto complete_facility(FacilityEvent&) -> bool;

    auto take_output() -> Output;

    auto poll(mut_ref<CompletionSourceRelease> self, task::Context& cx) -> task::Poll<Output>;

private:
    void clear_waker();
};

export class IoOperation {
public:
    using Output = io::Result<IoCompletion>;

private:
    IoOperationArc m_state;
#if LITO_FEAT_ASYNC_DEBUG
    debug::WaitGuard debug_wait_;
#endif
    bool m_completed { false };

    explicit IoOperation(IoOperationArc state);

    auto attach_consumer(IoOperationConsumer consumer, Option<task::Waker> waker = None())
        -> IoOperationLifecycle;

    auto take_result() -> Output;

    void cancel();

public:
    IoOperation(const IoOperation&)                    = delete;
    auto operator=(const IoOperation&) -> IoOperation& = delete;
    IoOperation(IoOperation&&) noexcept                = default;
    auto operator=(IoOperation&& other) noexcept -> IoOperation&;

    ~IoOperation();

    // A persistent owner can retain an operation after its current polling waiter is dropped.
    auto take_poll_waker() -> Option<task::Waker>;

    static auto read(const CompletionSource& source, usize len) -> IoOperation;

    static auto read_at(const CompletionSource& source, usize len, u64 offset) -> IoOperation;

    static auto read_into(const CompletionSource& source, bytes::BytesMut buffer) -> IoOperation;

    /// Consuming a completion, including an error, is required before reclaiming its storage.
    auto take_read_buffer() -> bytes::BytesMut;

    static auto write(const CompletionSource& source, bytes::Bytes data) -> IoOperation;

    static auto recv_datagram_into(const CompletionSource& source,
                                   bytes::BytesMut         buffer,
                                   net::SocketAddr         local) -> IoOperation;

    /// Only after consuming the receive result, including errors; never while the backend owns it.
    auto take_datagram_buffer() -> bytes::BytesMut;

    static auto recv_datagram(const CompletionSource& source, usize length, net::SocketAddr local)
        -> io::Result<IoOperation>;
    static auto send_datagram(const CompletionSource& source,
                              bytes::Bytes            data,
                              net::DatagramTarget     target,
                              net::SocketAddr         local) -> io::Result<IoOperation>;

    static auto connect(const CompletionSource& source, net::SocketAddr address) -> IoOperation;

    static auto accept(const CompletionSource&         source,
                       net::SocketAddr                 address,
                       Option<os::socket::OwnedSocket> accepted_socket,
                       usize                           address_buffer_size) -> IoOperation;

    auto advance(AwaitContext& cx) -> AwaitTransition;

    auto submit_completion(FacilityCompletionToken token) -> FacilityCompletionSubmitResult;

    auto complete_facility(FacilityEvent& event) -> bool;

    auto take_output() -> Output;

    auto poll(mut_ref<IoOperation> self, task::Context& cx) -> task::Poll<Output>;
};

template<>
struct AwaitableTraits<IoOperation> {
    using Output = IoOperation::Output;

    static auto make_suspension(IoOperation&& operation) {
        return AwaitSuspension<IoOperation> { rstd::move(operation) };
    }
};

template<>
struct AwaitableTraits<CompletionSourceRelease> {
    using Output = CompletionSourceRelease::Output;

    static auto make_suspension(CompletionSourceRelease&& release) {
        return AwaitSuspension<CompletionSourceRelease> { rstd::move(release) };
    }
};

} // namespace rstd::async

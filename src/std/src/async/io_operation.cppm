module;
#include <rstd/macro.hpp>

export module rstd:async.io_operation;
export import :async.forward;
export import :bytes;
export import :io.error;
import :async.awaitable;
import :async.poll;
import :async.runtime_core;
import :net.socket_addr;
import :os.fd;
import :os.handle;
import :os.socket;
import :sync;
import rstd.alloc;

using namespace rstd;

namespace rstd::async
{

inline constexpr rstd::uintptr_t IO_COMPLETION_FACILITY_ID { ~rstd::uintptr_t(0) -
                                                             rstd::uintptr_t(2) };
inline constexpr rstd::uintptr_t IO_SOURCE_RELEASE_FACILITY_ID { ~rstd::uintptr_t(0) -
                                                                 rstd::uintptr_t(3) };

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
    Option<CompletionSourceBinding> binding {};
    usize                           active_operations {};
    Option<FacilityCompletionToken> release_token {};
    Option<io::error::Error>        release_error {};
    bool                            release_requested { false };
    bool                            release_submitted { false };
    bool                            released { false };
};

struct CompletionSourceState {
    os::fd::RawFd                       fd { os::fd::INVALID_RAW_FD };
    CompletionSourceKind                kind { CompletionSourceKind::File };
    sync::Mutex<CompletionSourceFields> fields;

    CompletionSourceState(os::fd::RawFd fd, CompletionSourceKind kind)
        : fd(fd), kind(kind), fields(CompletionSourceFields {}) {}

    ~CompletionSourceState();
};

export class CompletionSourceRelease;

export class CompletionSource {
    CompletionSourceArc m_state;

    CompletionSource(os::fd::RawFd fd, CompletionSourceKind kind)
        : m_state(CompletionSourceArc::make(fd, kind)) {}

    explicit CompletionSource(CompletionSourceArc state): m_state(rstd::move(state)) {}

    auto bind_worker(WorkerHandle current) const -> WorkerHandle {
        auto fields = m_state->fields.lock().unwrap_unchecked();
        if (fields->released || fields->release_requested) {
            rstd::panic { "completion source used after release" };
        }
        if (fields->binding.is_none()) {
            auto source_key = current.allocate_source_key();
            fields->binding.insert(CompletionSourceBinding { current.clone(), source_key });
        }
        return fields->binding->worker.clone();
    }

    auto source_key() const -> SourceKey {
        auto fields = m_state->fields.lock().unwrap_unchecked();
        if (fields->binding.is_none()) rstd::panic { "completion source is not bound" };
        return fields->binding->source_key;
    }

    void begin_operation() const {
        auto fields = m_state->fields.lock().unwrap_unchecked();
        if (fields->released || fields->release_requested) {
            rstd::panic { "completion source used after release" };
        }
        ++fields->active_operations;
    }

    friend struct IoOperationState;

public:
    CompletionSource(const CompletionSource&)                        = delete;
    auto operator=(const CompletionSource&) -> CompletionSource&     = delete;
    CompletionSource(CompletionSource&&) noexcept                    = default;
    auto operator=(CompletionSource&&) noexcept -> CompletionSource& = default;

    static auto file(os::fd::BorrowedFd fd) noexcept -> CompletionSource {
        return CompletionSource { fd.as_raw_fd(), CompletionSourceKind::File };
    }

#if RSTD_OS_WINDOWS
    static auto file(os::handle::BorrowedHandle handle) noexcept -> CompletionSource {
        return CompletionSource { handle.as_raw_handle(), CompletionSourceKind::File };
    }
#endif

    static auto socket(os::fd::BorrowedFd fd) noexcept -> CompletionSource {
        return CompletionSource { fd.as_raw_fd(), CompletionSourceKind::Socket };
    }

    static auto socket(os::socket::BorrowedSocket socket) noexcept -> CompletionSource {
        return CompletionSource { socket.as_raw_socket(), CompletionSourceKind::Socket };
    }

    auto clone() const -> CompletionSource { return CompletionSource { m_state.clone() }; }
    auto as_raw_fd() const noexcept -> os::fd::RawFd { return m_state->fd; }
    auto kind() const noexcept -> CompletionSourceKind { return m_state->kind; }
    auto is_bound() const -> bool {
        return m_state->fields.lock().unwrap_unchecked()->binding.is_some();
    }
    auto release() && -> CompletionSourceRelease;
};

export class IoCompletion {
    usize                           m_transferred {};
    u32                             m_flags {};
    bytes::Bytes                    m_data;
    Option<os::socket::OwnedSocket> m_socket {};

public:
    IoCompletion(usize                           transferred,
                 u32                             flags,
                 bytes::Bytes                    data,
                 Option<os::socket::OwnedSocket> socket = None())
        : m_transferred(transferred),
          m_flags(flags),
          m_data(rstd::move(data)),
          m_socket(rstd::move(socket)) {}

    auto transferred() const noexcept -> usize { return m_transferred; }
    auto flags() const noexcept -> u32 { return m_flags; }
    auto data() const noexcept [[clang::lifetimebound]] -> slice<u8> { return m_data.as_slice(); }
    auto into_data() && -> bytes::Bytes { return rstd::move(m_data); }
    auto has_socket() const noexcept -> bool { return m_socket.is_some(); }
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
    Option<WorkerHandle>             worker {};
    Option<OperationKey>             key {};
    Option<FacilityCompletionToken>  token {};
    Option<task::Waker>              waker {};
    Option<io::Result<IoCompletion>> result {};
    IoOperationLifecycle             lifecycle { IoOperationLifecycle::Created };
    IoOperationConsumer              consumer { IoOperationConsumer::None };
    bool                             cancel_requested { false };
    bool                             source_active { true };
};

struct IoOperationState {
    struct Read {
        bytes::BytesMut buffer;
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

    using Payload = Choice<choice_case<PollOperationKind::Read, Read>,
                           choice_case<PollOperationKind::Write, Write>,
                           choice_case<PollOperationKind::Connect, Connect>,
                           choice_case<PollOperationKind::Accept, Accept>>;

    CompletionSource               source;
    Payload                        payload;
    sync::Mutex<IoOperationFields> fields;

    IoOperationState(CompletionSource source, usize read_len)
        : source(rstd::move(source)),
          payload(Payload::with<PollOperationKind::Read>(Read { bytes::BytesMut::make() })),
          fields(IoOperationFields {}) {
        this->source.begin_operation();
        auto buffer = bytes::BytesMut::with_capacity(read_len);
        buffer.resize(read_len, u8 {});
        payload.as<PollOperationKind::Read>().buffer = rstd::move(buffer);
    }

    IoOperationState(CompletionSource source, bytes::Bytes data)
        : source(rstd::move(source)),
          payload(Payload::with<PollOperationKind::Write>(Write { rstd::move(data) })),
          fields(IoOperationFields {}) {
        this->source.begin_operation();
    }

    IoOperationState(CompletionSource source, net::SocketAddr address)
        : source(rstd::move(source)),
          payload(Payload::with<PollOperationKind::Connect>(Connect { address })),
          fields(IoOperationFields {}) {
        this->source.begin_operation();
    }

    IoOperationState(CompletionSource                source,
                     net::SocketAddr                 address,
                     Option<os::socket::OwnedSocket> accepted_socket,
                     bytes::BytesMut                 accept_address_buffer)
        : source(rstd::move(source)),
          payload(Payload::with<PollOperationKind::Accept>(Accept {
              address,
              rstd::move(accepted_socket),
              rstd::move(accept_address_buffer),
          })),
          fields(IoOperationFields {}) {
        this->source.begin_operation();
    }

    ~IoOperationState();

    auto poll_operation() -> PollOperation {
        auto attach_source = [this](PollOperation operation) {
            operation.set_source_key(source.source_key());
            return operation;
        };
        switch (payload.which()) {
        case PollOperationKind::Read: {
            auto& buffer = payload.as<PollOperationKind::Read>().buffer;
            return attach_source(
                source.kind() == CompletionSourceKind::Socket
                    ? PollOperation::read_socket(source.as_raw_fd(), buffer.data(), buffer.len())
                    : PollOperation::read(source.as_raw_fd(), buffer.data(), buffer.len()));
        }
        case PollOperationKind::Write: {
            auto& buffer = payload.as<PollOperationKind::Write>().buffer;
            return attach_source(
                source.kind() == CompletionSourceKind::Socket
                    ? PollOperation::write_socket(source.as_raw_fd(), buffer.data(), buffer.len())
                    : PollOperation::write(source.as_raw_fd(), buffer.data(), buffer.len()));
        }
        case PollOperationKind::Connect:
            return attach_source(PollOperation::connect_socket(
                source.as_raw_fd(), payload.as<PollOperationKind::Connect>().address));
        case PollOperationKind::Accept: {
            auto& accept = payload.as<PollOperationKind::Accept>();
            auto  accepted =
                accept.socket.is_some() ? accept.socket->as_raw_socket() : os::fd::INVALID_RAW_FD;
            return attach_source(PollOperation::accept_socket(source.as_raw_fd(),
                                                              accepted,
                                                              accept.address,
                                                              accept.address_buffer.data(),
                                                              accept.address_buffer.len()));
        }
        }
        rstd::unreachable();
    }

    auto finish(PollCompletion completion) -> io::Result<IoCompletion> {
        auto flags = completion.flags();
        if (payload.is<PollOperationKind::Accept>()) {
            if (! completion.has_resource()) {
                return Err(io::error::Error::from_kind(
                    io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
            }
            auto  resource     = completion.take_resource();
            auto& accept_state = payload.as<PollOperationKind::Accept>();
            auto  accepted     = Option<os::socket::OwnedSocket> {};
            if (accept_state.socket.is_some()) {
                if (accept_state.socket->as_raw_socket() != resource) {
                    auto unexpected = os::socket::OwnedSocket::from_raw_socket(resource);
                    return Err(io::error::Error::from_kind(
                        io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
                }
                accepted = Some(accept_state.socket.take().unwrap_unchecked());
            } else {
                accepted = Some(os::socket::OwnedSocket::from_raw_socket(resource));
            }
            return Ok(IoCompletion { usize(), flags, bytes::Bytes::make(), rstd::move(accepted) });
        }
        auto result = completion.result();
        if (result < isize()) {
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
        }
        auto transferred = usize(result.to_primitive());
        if (payload.is<PollOperationKind::Read>()) {
            auto buffer = rstd::move(payload.as<PollOperationKind::Read>().buffer);
            if (transferred > buffer.len()) {
                return Err(io::error::Error::from_kind(
                    io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
            }
            buffer.truncate(transferred);
            return Ok(IoCompletion { transferred, flags, buffer.freeze() });
        }
        return Ok(IoCompletion { transferred, flags, bytes::Bytes::make() });
    }

    auto finish(PollEventData event) -> io::Result<IoCompletion> {
        if (event.kind() == PollEventKind::Completion) {
            auto completion = event.take_completion();
            if (completion.is_error()) return Err(completion.take_error());
            return finish(rstd::move(completion));
        }
        if (event.kind() == PollEventKind::BackendError) {
            return Err(event.has_backend_error()
                           ? event.take_backend_error()
                           : io::error::Error::from_kind(
                                 io::error::ErrorKind { io::error::ErrorKind::Other }));
        }
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
    }

    auto required_capability() const noexcept -> PollCapability {
        return source.kind() == CompletionSourceKind::Socket ? PollCapability::SocketCompletion
                                                             : PollCapability::FileCompletion;
    }

    auto bind_worker(WorkerHandle current) const -> WorkerHandle {
        return source.bind_worker(rstd::move(current));
    }

    auto source_state() const -> CompletionSourceArc { return source.m_state.clone(); }
};

using IoOperationArc = sync::Arc<IoOperationState>;

auto make_io_operation_owner(const IoOperationArc& state) -> PollEventOwner;
void try_submit_completion_source_release(const CompletionSourceArc& state);

void finish_completion_source_activity(CompletionSourceArc state) {
    bool submit_release = false;
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->active_operations == usize()) {
            rstd::panic { "completion source active operation count underflow" };
        }
        --fields->active_operations;
        submit_release = fields->release_requested && fields->active_operations == usize();
    }
    if (submit_release) try_submit_completion_source_release(state);
}

void publish_io_operation_event(const IoOperationArc& state, PollEventData event) {
    auto token  = Option<FacilityCompletionToken> {};
    auto waker  = Option<task::Waker> {};
    auto source = Option<CompletionSourceArc> {};
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->lifecycle == IoOperationLifecycle::Ready ||
            fields->lifecycle == IoOperationLifecycle::Consumed) {
            return;
        }
        fields->result    = Some(state->finish(rstd::move(event)));
        fields->lifecycle = IoOperationLifecycle::Ready;
        token             = fields->token.take();
        waker             = fields->waker.take();
        if (fields->source_active) {
            fields->source_active = false;
            source                = Some(state->source_state());
        }
    }
    if (source.is_some()) {
        finish_completion_source_activity(rstd::move(source).unwrap_unchecked());
    }
    if (token.is_some()) {
        (void)rstd::move(token).unwrap_unchecked().complete(FacilityEventKind::Ready);
    }
    if (waker.is_some()) {
        rstd::move(waker).unwrap_unchecked().wake();
    }
}

IoOperationState::~IoOperationState() {
    bool active = false;
    {
        auto current           = fields.lock().unwrap_unchecked();
        active                 = current->source_active;
        current->source_active = false;
    }
    if (active) finish_completion_source_activity(source_state());
}

auto completion_source_owner_clone(voidp data) -> RawPollEventOwner;

void finish_completion_source_release(const CompletionSourceArc& state,
                                      Option<io::error::Error>   error) {
    auto token  = Option<FacilityCompletionToken> {};
    bool failed = error.is_some();
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->released) return;
        fields->released          = true;
        fields->release_submitted = false;
        fields->release_error     = rstd::move(error);
        if (! failed) fields->binding = None<CompletionSourceBinding>();
        token = fields->release_token.take();
    }
    if (token.is_some()) {
        (void)rstd::move(token).unwrap_unchecked().complete(failed ? FacilityEventKind::Error
                                                                   : FacilityEventKind::Ready);
    }
}

void completion_source_owner_dispatch(voidp data, PollEventData event) {
    auto state =
        CompletionSourceArc::from_raw(::alloc::sync::ArcRaw<CompletionSourceState>::from_raw(data));
    auto error = Option<io::error::Error> {};
    if (event.has_backend_error()) {
        error = Some(event.take_backend_error());
    } else if (event.kind() != PollEventKind::SourceReleased) {
        error = Some(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
    }
    finish_completion_source_release(state, rstd::move(error));
    (void)rstd::move(state).into_raw().into_raw();
}

void completion_source_owner_drop(voidp data) {
    auto state =
        CompletionSourceArc::from_raw(::alloc::sync::ArcRaw<CompletionSourceState>::from_raw(data));
    (void)state;
}

const RawPollEventOwnerVTable COMPLETION_SOURCE_OWNER_VTABLE {
    &completion_source_owner_clone,
    &completion_source_owner_dispatch,
    &completion_source_owner_drop,
};

auto completion_source_owner_clone(voidp data) -> RawPollEventOwner {
    auto state =
        CompletionSourceArc::from_raw(::alloc::sync::ArcRaw<CompletionSourceState>::from_raw(data));
    auto cloned = state.clone();
    (void)rstd::move(state).into_raw().into_raw();
    return RawPollEventOwner::from_raw_parts(rstd::move(cloned).into_raw().into_raw(),
                                             rstd::addressof(COMPLETION_SOURCE_OWNER_VTABLE));
}

auto make_completion_source_owner(const CompletionSourceArc& state) -> PollEventOwner {
    auto owned = state.clone();
    return PollEventOwner::from_raw(RawPollEventOwner::from_raw_parts(
        rstd::move(owned).into_raw().into_raw(), rstd::addressof(COMPLETION_SOURCE_OWNER_VTABLE)));
}

void try_submit_completion_source_release(const CompletionSourceArc& state) {
    auto worker             = Option<WorkerHandle> {};
    auto key                = SourceKey {};
    auto fd                 = os::fd::INVALID_RAW_FD;
    bool finish_immediately = false;
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (! fields->release_requested || fields->released || fields->release_submitted ||
            fields->active_operations != usize()) {
            return;
        }
        if (fields->binding.is_none()) {
            finish_immediately = true;
        } else {
            fields->release_submitted = true;
            worker                    = Some(fields->binding->worker.clone());
            key                       = fields->binding->source_key;
            fd                        = state->fd;
        }
    }
    if (finish_immediately) {
        finish_completion_source_release(state, None<io::error::Error>());
        return;
    }
    auto command =
        PollCommand::release_completion_source(key, fd, make_completion_source_owner(state));
    if (worker->submit_poll(rstd::move(command)).is_err()) {
        finish_completion_source_release(state,
                                         Some(io::error::Error::from_kind(io::error::ErrorKind {
                                             io::error::ErrorKind::NotConnected })));
    }
}

void store_io_operation_error(const IoOperationArc& state, io::error::Error error) {
    auto source = Option<CompletionSourceArc> {};
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->lifecycle == IoOperationLifecycle::Ready ||
            fields->lifecycle == IoOperationLifecycle::Consumed) {
            return;
        }
        fields->result    = Some(io::Result<IoCompletion>(Err(rstd::move(error))));
        fields->lifecycle = IoOperationLifecycle::Ready;
        if (fields->source_active) {
            fields->source_active = false;
            source                = Some(state->source_state());
        }
    }
    if (source.is_some()) {
        finish_completion_source_activity(rstd::move(source).unwrap_unchecked());
    }
}

auto io_operation_owner_clone(voidp data) -> RawPollEventOwner;

void io_operation_owner_dispatch(voidp data, PollEventData event) {
    auto state = IoOperationArc::from_raw(::alloc::sync::ArcRaw<IoOperationState>::from_raw(data));
    publish_io_operation_event(state, rstd::move(event));
    (void)rstd::move(state).into_raw().into_raw();
}

void io_operation_owner_drop(voidp data) {
    auto state = IoOperationArc::from_raw(::alloc::sync::ArcRaw<IoOperationState>::from_raw(data));
    (void)state;
}

const RawPollEventOwnerVTable IO_OPERATION_OWNER_VTABLE {
    &io_operation_owner_clone,
    &io_operation_owner_dispatch,
    &io_operation_owner_drop,
};

auto io_operation_owner_clone(voidp data) -> RawPollEventOwner {
    auto state  = IoOperationArc::from_raw(::alloc::sync::ArcRaw<IoOperationState>::from_raw(data));
    auto cloned = state.clone();
    (void)rstd::move(state).into_raw().into_raw();
    return RawPollEventOwner::from_raw_parts(rstd::move(cloned).into_raw().into_raw(),
                                             rstd::addressof(IO_OPERATION_OWNER_VTABLE));
}

auto make_io_operation_owner(const IoOperationArc& state) -> PollEventOwner {
    auto owned = state.clone();
    return PollEventOwner::from_raw(RawPollEventOwner::from_raw_parts(
        rstd::move(owned).into_raw().into_raw(), rstd::addressof(IO_OPERATION_OWNER_VTABLE)));
}

CompletionSourceState::~CompletionSourceState() {
    auto current = fields.lock().unwrap_unchecked()->binding.take();
    if (current.is_none()) return;
    auto bound = rstd::move(current).unwrap_unchecked();
    (void)bound.worker.submit_poll(PollCommand::release_completion_source(bound.source_key, fd));
}

void completion_source_release_cancel(voidp data) {
    auto state =
        CompletionSourceArc::from_raw(::alloc::sync::ArcRaw<CompletionSourceState>::from_raw(data));
    state->fields.lock().unwrap_unchecked()->release_token = None<FacilityCompletionToken>();
}

void completion_source_release_drop(voidp data) {
    auto state =
        CompletionSourceArc::from_raw(::alloc::sync::ArcRaw<CompletionSourceState>::from_raw(data));
    (void)state;
}

const RawFacilityCancellationVTable COMPLETION_SOURCE_RELEASE_CANCELLATION_VTABLE {
    &completion_source_release_cancel,
    &completion_source_release_drop,
};

auto make_completion_source_release_cancellation(const CompletionSourceArc& state,
                                                 FacilityToken token) -> FacilityCancellation {
    auto owned = state.clone();
    return FacilityCancellation::from_raw(
        token,
        RawFacilityCancellation::from_raw_parts(
            rstd::move(owned).into_raw().into_raw(),
            rstd::addressof(COMPLETION_SOURCE_RELEASE_CANCELLATION_VTABLE)));
}

export class CompletionSourceRelease {
    CompletionSourceArc m_state;
    bool                m_completed { false };

    explicit CompletionSourceRelease(CompletionSourceArc state): m_state(rstd::move(state)) {}

    friend class CompletionSource;

public:
    using Output = io::Result<empty>;

    CompletionSourceRelease(const CompletionSourceRelease&)                        = delete;
    auto operator=(const CompletionSourceRelease&) -> CompletionSourceRelease&     = delete;
    CompletionSourceRelease(CompletionSourceRelease&&) noexcept                    = default;
    auto operator=(CompletionSourceRelease&&) noexcept -> CompletionSourceRelease& = default;

    auto advance(AwaitContext& cx) -> AwaitTransition {
        if (cx.execution_domain() == ExecutionDomainKind::ExternalExecutor) {
            return AwaitTransition::return_to_owner();
        }
        if (m_completed) rstd::panic { "completion source release advanced after completion" };
        {
            auto fields               = m_state->fields.lock().unwrap_unchecked();
            fields->release_requested = true;
            if (fields->released ||
                (fields->active_operations == usize() && fields->binding.is_none())) {
                fields->released = true;
                m_completed      = true;
                return AwaitTransition::continue_();
            }
        }
        return AwaitTransition::submit_completion(IO_SOURCE_RELEASE_FACILITY_ID);
    }

    auto submit_completion(FacilityCompletionToken token) -> FacilityCompletionSubmitResult {
        auto identity = token.token();
        bool ready    = false;
        {
            auto fields = m_state->fields.lock().unwrap_unchecked();
            if (! fields->release_requested || fields->release_token.is_some()) {
                return FacilityCompletionSubmitResult::rejected(rstd::move(token));
            }
            ready                 = fields->released;
            fields->release_token = Some(rstd::move(token));
        }
        if (ready) {
            auto completed = Option<FacilityCompletionToken> {};
            auto failed    = false;
            {
                auto fields = m_state->fields.lock().unwrap_unchecked();
                failed      = fields->release_error.is_some();
                completed   = fields->release_token.take();
            }
            (void)rstd::move(completed).unwrap_unchecked().complete(
                failed ? FacilityEventKind::Error : FacilityEventKind::Ready);
        } else {
            try_submit_completion_source_release(m_state);
        }
        return FacilityCompletionSubmitResult::accepted(
            make_completion_source_release_cancellation(m_state, identity));
    }

    auto complete_facility(FacilityEvent&) -> bool {
        return m_state->fields.lock().unwrap_unchecked()->released;
    }

    auto take_output() -> Output {
        if (! m_completed) {
            rstd::panic { "completion source release output taken before completion" };
        }
        auto fields = m_state->fields.lock().unwrap_unchecked();
        if (fields->release_error.is_some()) {
            return Err(fields->release_error.take().unwrap_unchecked());
        }
        return Ok(empty {});
    }
};

auto CompletionSource::release() && -> CompletionSourceRelease {
    return CompletionSourceRelease { rstd::move(m_state) };
}

void request_io_operation_cancel(const IoOperationArc& state) {
    auto worker = Option<WorkerHandle> {};
    auto key    = Option<OperationKey> {};
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->lifecycle == IoOperationLifecycle::Ready ||
            fields->lifecycle == IoOperationLifecycle::Consumed || fields->cancel_requested) {
            return;
        }
        fields->cancel_requested = true;
        if (fields->worker.is_some() && fields->key.is_some()) {
            worker = Some(fields->worker->clone());
            key    = fields->key;
        }
    }
    if (worker.is_some() && key.is_some()) {
        auto command = PollCommand::cancel_operation(*key);
        if (CURRENT_RUNTIME != nullptr && CURRENT_RUNTIME_WORKER_CONTEXT != nullptr) {
            auto current = CURRENT_RUNTIME->current_poll_worker();
            if (current.is_ok() && worker->is_same(*current)) {
                (void)CURRENT_RUNTIME->defer_current_poll(rstd::move(command));
                return;
            }
        }
        (void)worker->submit_poll(rstd::move(command));
    }
}

void io_operation_cancellation_cancel(voidp data) {
    auto state = IoOperationArc::from_raw(::alloc::sync::ArcRaw<IoOperationState>::from_raw(data));
    request_io_operation_cancel(state);
}

void io_operation_cancellation_drop(voidp data) {
    auto state = IoOperationArc::from_raw(::alloc::sync::ArcRaw<IoOperationState>::from_raw(data));
    (void)state;
}

const RawFacilityCancellationVTable IO_OPERATION_CANCELLATION_VTABLE {
    &io_operation_cancellation_cancel,
    &io_operation_cancellation_drop,
};

auto make_io_operation_cancellation(const IoOperationArc& state, FacilityToken token)
    -> FacilityCancellation {
    auto owned = state.clone();
    return FacilityCancellation::from_raw(
        token,
        RawFacilityCancellation::from_raw_parts(rstd::move(owned).into_raw().into_raw(),
                                                rstd::addressof(IO_OPERATION_CANCELLATION_VTABLE)));
}

enum class IoOperationSubmitStatus
{
    Accepted,
    Rejected,
    Unsupported,
};

auto ensure_io_operation_submitted(const IoOperationArc& state) -> IoOperationSubmitStatus {
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->lifecycle != IoOperationLifecycle::Created) {
            return fields->lifecycle == IoOperationLifecycle::Submitted
                       ? IoOperationSubmitStatus::Accepted
                       : IoOperationSubmitStatus::Rejected;
        }
    }

    if (CURRENT_RUNTIME == nullptr) {
        rstd::panic { "async::IoOperation started without an async runtime" };
    }
    if (! CURRENT_RUNTIME->io_enabled()) {
        store_io_operation_error(state,
                                 io::error::Error::from_kind(
                                     io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
        return IoOperationSubmitStatus::Unsupported;
    }
    if (! has_current_runtime_worker()) {
        store_io_operation_error(state,
                                 io::error::Error::from_kind(
                                     io::error::ErrorKind { io::error::ErrorKind::NotConnected }));
        return IoOperationSubmitStatus::Rejected;
    }

    auto current_result = CURRENT_RUNTIME->current_poll_worker();
    if (current_result.is_err()) {
        store_io_operation_error(state, rstd::move(current_result).unwrap_err_unchecked());
        return IoOperationSubmitStatus::Rejected;
    }
    auto current = rstd::move(current_result).unwrap_unchecked();
    auto worker  = state->bind_worker(current.clone());
    if (! worker.poll_capabilities().contains(state->required_capability())) {
        store_io_operation_error(state,
                                 io::error::Error::from_kind(
                                     io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
        return IoOperationSubmitStatus::Unsupported;
    }

    auto local   = worker.is_same(current);
    auto key     = OperationKey {};
    auto command = Option<PollCommand> {};
    if (local) {
        auto reserved = CURRENT_RUNTIME->reserve_current_operation(make_io_operation_owner(state));
        if (reserved.is_err()) {
            store_io_operation_error(state, rstd::move(reserved).unwrap_err_unchecked());
            return IoOperationSubmitStatus::Rejected;
        }
        key     = rstd::move(reserved).unwrap_unchecked();
        command = Some(PollCommand::submit_operation(key, state->poll_operation()));
    } else {
        key     = worker.allocate_operation_key();
        command = Some(PollCommand::submit_remote_operation(
            key, state->poll_operation(), make_io_operation_owner(state)));
    }
    auto submitted = [&]() -> Result<empty, PollCommand> {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->lifecycle != IoOperationLifecycle::Created) {
            return Err(rstd::move(command).unwrap_unchecked());
        }
        fields->worker    = Some(worker.clone());
        fields->key       = Some(key);
        fields->lifecycle = IoOperationLifecycle::Submitted;
        auto result =
            local ? CURRENT_RUNTIME->defer_current_poll(rstd::move(command).unwrap_unchecked())
                  : worker.submit_poll(rstd::move(command).unwrap_unchecked());
        if (result.is_err()) {
            fields->worker = None<WorkerHandle>();
            fields->key    = None<OperationKey>();
        } else if (fields->cancel_requested) {
            auto cancel = PollCommand::cancel_operation(key);
            if (local) {
                (void)CURRENT_RUNTIME->defer_current_poll(rstd::move(cancel));
            } else {
                (void)worker.submit_poll(rstd::move(cancel));
            }
        }
        return result;
    }();
    if (submitted.is_err()) {
        if (local) {
            CURRENT_RUNTIME->abandon_current_operation(key);
        } else {
            auto rejected = rstd::move(submitted).unwrap_err_unchecked();
            auto event    = rstd::move(rejected).into_error_event(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::NotConnected }));
            event.dispatch();
        }
        if (local) {
            store_io_operation_error(state,
                                     io::error::Error::from_kind(io::error::ErrorKind {
                                         io::error::ErrorKind::NotConnected }));
        }
        return IoOperationSubmitStatus::Rejected;
    }
    return IoOperationSubmitStatus::Accepted;
}

export class IoOperation {
public:
    using Output = io::Result<IoCompletion>;

private:
    IoOperationArc m_state;
    bool           m_completed { false };

    explicit IoOperation(IoOperationArc state): m_state(rstd::move(state)) {}

    auto attach_consumer(IoOperationConsumer consumer, Option<task::Waker> waker = None())
        -> IoOperationLifecycle {
        auto fields = m_state->fields.lock().unwrap_unchecked();
        if (fields->consumer == IoOperationConsumer::None) {
            fields->consumer = consumer;
        } else if (fields->consumer != consumer) {
            rstd::panic { "async::IoOperation cannot mix direct await and Future poll" };
        }
        if (consumer == IoOperationConsumer::Poll &&
            fields->lifecycle != IoOperationLifecycle::Ready &&
            fields->lifecycle != IoOperationLifecycle::Consumed) {
            fields->waker = rstd::move(waker);
        }
        return fields->lifecycle;
    }

    auto take_result() -> Output {
        auto fields = m_state->fields.lock().unwrap_unchecked();
        if (fields->lifecycle != IoOperationLifecycle::Ready || fields->result.is_none()) {
            rstd::panic { "async::IoOperation output taken before completion" };
        }
        fields->lifecycle = IoOperationLifecycle::Consumed;
        return fields->result.take().unwrap_unchecked();
    }

    void cancel() {
        if (! m_state || m_completed) return;
        {
            auto fields   = m_state->fields.lock().unwrap_unchecked();
            fields->waker = None<task::Waker>();
        }
        request_io_operation_cancel(m_state);
    }

public:
    IoOperation(const IoOperation&)                    = delete;
    auto operator=(const IoOperation&) -> IoOperation& = delete;
    IoOperation(IoOperation&&) noexcept                = default;
    auto operator=(IoOperation&& other) noexcept -> IoOperation& {
        if (this != &other) {
            cancel();
            m_state     = rstd::move(other.m_state);
            m_completed = rstd::exchange(other.m_completed, false);
        }
        return *this;
    }

    ~IoOperation() { cancel(); }

    static auto read(const CompletionSource& source, usize len) -> IoOperation {
        return IoOperation { IoOperationArc::make(source.clone(), len) };
    }

    static auto write(const CompletionSource& source, bytes::Bytes data) -> IoOperation {
        return IoOperation { IoOperationArc::make(source.clone(), rstd::move(data)) };
    }

    static auto connect(const CompletionSource& source, net::SocketAddr address) -> IoOperation {
        return IoOperation { IoOperationArc::make(source.clone(), address) };
    }

    static auto accept(const CompletionSource&         source,
                       net::SocketAddr                 address,
                       Option<os::socket::OwnedSocket> accepted_socket,
                       usize                           address_buffer_size) -> IoOperation {
        auto buffer = bytes::BytesMut::with_capacity(address_buffer_size);
        buffer.resize(address_buffer_size, u8 {});
        return IoOperation { IoOperationArc::make(
            source.clone(), address, rstd::move(accepted_socket), rstd::move(buffer)) };
    }

    auto advance(AwaitContext& cx) -> AwaitTransition {
        if (cx.execution_domain() == ExecutionDomainKind::ExternalExecutor) {
            return AwaitTransition::return_to_owner();
        }
        if (m_completed) rstd::panic { "async::IoOperation advanced after completion" };
        auto lifecycle = attach_consumer(IoOperationConsumer::Direct);
        if (lifecycle == IoOperationLifecycle::Ready) {
            m_completed = true;
            return AwaitTransition::continue_();
        }
        if (lifecycle == IoOperationLifecycle::Consumed) {
            rstd::panic { "async::IoOperation advanced after result consumption" };
        }
        if (lifecycle == IoOperationLifecycle::Submitted) return AwaitTransition::suspend();
        return AwaitTransition::submit_completion(IO_COMPLETION_FACILITY_ID);
    }

    auto submit_completion(FacilityCompletionToken token) -> FacilityCompletionSubmitResult {
        auto identity = token.token();
        {
            auto fields = m_state->fields.lock().unwrap_unchecked();
            if (fields->consumer != IoOperationConsumer::Direct ||
                fields->lifecycle != IoOperationLifecycle::Created || fields->token.is_some()) {
                return FacilityCompletionSubmitResult::rejected(rstd::move(token));
            }
            fields->token = Some(rstd::move(token));
        }

        auto submitted = ensure_io_operation_submitted(m_state);
        if (submitted == IoOperationSubmitStatus::Accepted) {
            return FacilityCompletionSubmitResult::accepted(
                make_io_operation_cancellation(m_state, identity));
        }

        auto returned = Option<FacilityCompletionToken> {};
        {
            auto fields = m_state->fields.lock().unwrap_unchecked();
            returned    = fields->token.take();
        }
        if (submitted == IoOperationSubmitStatus::Unsupported) {
            return FacilityCompletionSubmitResult::unsupported(
                rstd::move(returned).unwrap_unchecked());
        }
        return FacilityCompletionSubmitResult::rejected(rstd::move(returned).unwrap_unchecked());
    }

    auto complete_facility(FacilityEvent& event) -> bool {
        if (m_completed) return false;
        {
            auto fields = m_state->fields.lock().unwrap_unchecked();
            if (fields->consumer != IoOperationConsumer::Direct) return false;
            if (fields->lifecycle == IoOperationLifecycle::Ready) return true;
            if (fields->lifecycle == IoOperationLifecycle::Consumed) return false;
        }
        auto kind = event.kind() == FacilityEventKind::Canceled
                        ? io::error::ErrorKind::Interrupted
                        : io::error::ErrorKind::NotConnected;
        store_io_operation_error(m_state,
                                 io::error::Error::from_kind(io::error::ErrorKind { kind }));
        return true;
    }

    auto take_output() -> Output {
        if (! m_completed) {
            rstd::panic { "async::IoOperation output taken before completion" };
        }
        return take_result();
    }

    auto poll(mut_ref<IoOperation> self, task::Context& cx) -> task::Poll<Output> {
        auto& operation = *self;
        if (operation.m_completed) {
            rstd::panic { "async::IoOperation polled after completion" };
        }

        if (CURRENT_RUNTIME == nullptr) {
            rstd::panic { "async::IoOperation polled without an async runtime" };
        }
        auto lifecycle =
            operation.attach_consumer(IoOperationConsumer::Poll, Some(cx.waker().clone()));
        if (lifecycle == IoOperationLifecycle::Ready) {
            operation.m_completed = true;
            return task::Poll<Output>::Ready(operation.take_result());
        }
        if (lifecycle == IoOperationLifecycle::Consumed) {
            rstd::panic { "async::IoOperation polled after result consumption" };
        }

        if (lifecycle == IoOperationLifecycle::Created) {
            (void)ensure_io_operation_submitted(operation.m_state);
        }
        lifecycle = operation.attach_consumer(IoOperationConsumer::Poll, Some(cx.waker().clone()));
        if (lifecycle == IoOperationLifecycle::Ready) {
            operation.m_completed = true;
            return task::Poll<Output>::Ready(operation.take_result());
        }
        return task::Poll<Output>::Pending();
    }
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

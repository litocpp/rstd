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

export enum class CompletionSourceKind {
    File,
    Socket,
};

struct IoOperationState;

struct CompletionSourceBinding {
    WorkerHandle worker;
    u64          source_key {};
};

struct CompletionSourceState {
    os::fd::RawFd                               fd { os::fd::INVALID_RAW_FD };
    CompletionSourceKind                        kind { CompletionSourceKind::File };
    sync::Mutex<Option<CompletionSourceBinding>> binding;

    CompletionSourceState(os::fd::RawFd fd, CompletionSourceKind kind)
        : fd(fd), kind(kind), binding(Option<CompletionSourceBinding> {}) {}
};

export class CompletionSource {
    sync::Arc<CompletionSourceState> m_state;

    CompletionSource(os::fd::RawFd fd, CompletionSourceKind kind)
        : m_state(sync::Arc<CompletionSourceState>::make(fd, kind)) {}

    explicit CompletionSource(sync::Arc<CompletionSourceState> state): m_state(rstd::move(state)) {}

    auto bind_worker(WorkerHandle current) const -> WorkerHandle {
        auto binding = m_state->binding.lock().unwrap_unchecked();
        if (binding->is_none()) {
            auto source_key = current.allocate_poll_key(PollKeyKind::Registration).value;
            binding->insert(CompletionSourceBinding { current.clone(), source_key });
        }
        return (**binding).worker.clone();
    }

    auto source_key() const -> u64 {
        auto binding = m_state->binding.lock().unwrap_unchecked();
        if (binding->is_none()) rstd::panic { "completion source is not bound" };
        return (**binding).source_key;
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
    auto is_bound() const -> bool { return m_state->binding.lock().unwrap_unchecked()->is_some(); }
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

struct IoOperationFields {
    Option<WorkerHandle>            worker {};
    Option<PollKey>                 key {};
    Option<FacilityCompletionToken> token {};
    Option<task::Waker>             waker {};
    Option<PollEventData>           event {};
    bool                            terminal { false };
    bool                            cancel_requested { false };
};

struct IoOperationState {
    CompletionSource                source;
    PollOperationKind               kind;
    Option<bytes::BytesMut>         read_buffer {};
    Option<bytes::Bytes>            write_buffer {};
    Option<net::SocketAddr>         address {};
    Option<os::socket::OwnedSocket> accepted_socket {};
    Option<bytes::BytesMut>         accept_address_buffer {};
    sync::Mutex<IoOperationFields>  fields;

    IoOperationState(CompletionSource source, usize read_len)
        : source(rstd::move(source)), kind(PollOperationKind::Read), fields(IoOperationFields {}) {
        auto buffer = bytes::BytesMut::with_capacity(read_len);
        buffer.resize(read_len, u8 {});
        read_buffer = Some(rstd::move(buffer));
    }

    IoOperationState(CompletionSource source, bytes::Bytes data)
        : source(rstd::move(source)),
          kind(PollOperationKind::Write),
          write_buffer(Some(rstd::move(data))),
          fields(IoOperationFields {}) {}

    IoOperationState(CompletionSource source, net::SocketAddr address)
        : source(rstd::move(source)),
          kind(PollOperationKind::Connect),
          address(Some(address)),
          fields(IoOperationFields {}) {}

    IoOperationState(CompletionSource                source,
                     net::SocketAddr                 address,
                     Option<os::socket::OwnedSocket> accepted_socket,
                     bytes::BytesMut                 accept_address_buffer)
        : source(rstd::move(source)),
          kind(PollOperationKind::Accept),
          address(Some(address)),
          accepted_socket(rstd::move(accepted_socket)),
          accept_address_buffer(Some(rstd::move(accept_address_buffer))),
          fields(IoOperationFields {}) {}

    auto poll_operation() -> PollOperation {
        auto operation = PollOperation {};
        if (kind == PollOperationKind::Read) {
            auto& buffer = *read_buffer;
            operation = source.kind() == CompletionSourceKind::Socket
                            ? PollOperation::read_socket(
                                  source.as_raw_fd(), buffer.data(), buffer.len())
                            : PollOperation::read(source.as_raw_fd(), buffer.data(), buffer.len());
        } else if (kind == PollOperationKind::Write) {
            auto& buffer = *write_buffer;
            operation = source.kind() == CompletionSourceKind::Socket
                            ? PollOperation::write_socket(
                                  source.as_raw_fd(), buffer.data(), buffer.len())
                            : PollOperation::write(
                                  source.as_raw_fd(), buffer.data(), buffer.len());
        } else if (kind == PollOperationKind::Connect) {
            operation = PollOperation::connect_socket(source.as_raw_fd(), *address);
        } else {
            auto& buffer = *accept_address_buffer;
            auto  accepted = accepted_socket.is_some() ? accepted_socket->as_raw_socket()
                                                       : os::fd::INVALID_RAW_FD;
            operation = PollOperation::accept_socket(
                source.as_raw_fd(), accepted, *address, buffer.data(), buffer.len());
        }
        operation.set_source_key(source.source_key());
        return operation;
    }

    auto finish(PollCompletion completion) -> io::Result<IoCompletion> {
        auto flags = completion.flags();
        if (kind == PollOperationKind::Accept) {
            if (! completion.has_resource()) {
                return Err(io::error::Error::from_kind(
                    io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
            }
            auto resource = completion.take_resource();
            auto accepted = Option<os::socket::OwnedSocket> {};
            if (accepted_socket.is_some()) {
                if (accepted_socket->as_raw_socket() != resource) {
                    auto unexpected = os::socket::OwnedSocket::from_raw_socket(resource);
                    return Err(io::error::Error::from_kind(
                        io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
                }
                accepted = Some(accepted_socket.take().unwrap_unchecked());
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
        if (kind == PollOperationKind::Read) {
            auto buffer = read_buffer.take().unwrap_unchecked();
            if (transferred > buffer.len()) {
                return Err(io::error::Error::from_kind(
                    io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
            }
            buffer.truncate(transferred);
            return Ok(IoCompletion { transferred, flags, buffer.freeze() });
        }
        return Ok(IoCompletion { transferred, flags, bytes::Bytes::make() });
    }

    auto bind_worker(WorkerHandle current) const -> WorkerHandle {
        return source.bind_worker(rstd::move(current));
    }
};

using IoOperationArc = sync::Arc<IoOperationState>;

auto make_io_operation_owner(const IoOperationArc& state) -> PollEventOwner;

void handle_io_operation_event(const IoOperationArc& state, PollEventData event) {
    auto token = Option<FacilityCompletionToken> {};
    auto waker = Option<task::Waker> {};
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->terminal) return;
        fields->terminal = true;
        token            = fields->token.take();
        if (token.is_none()) {
            fields->event = Some(rstd::move(event));
            waker         = fields->waker.take();
        }
    }
    if (token.is_some()) {
        (void)rstd::move(token).unwrap_unchecked().complete_poll(rstd::move(event));
    } else if (waker.is_some()) {
        rstd::move(waker).unwrap_unchecked().wake();
    }
}

auto io_operation_owner_clone(voidp data) -> RawPollEventOwner;

void io_operation_owner_dispatch(voidp data, PollEventData event) {
    auto state = IoOperationArc::from_raw(::alloc::sync::ArcRaw<IoOperationState>::from_raw(data));
    handle_io_operation_event(state, rstd::move(event));
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

void request_io_operation_cancel(const IoOperationArc& state) {
    auto worker = Option<WorkerHandle> {};
    auto key    = Option<PollKey> {};
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->terminal || fields->cancel_requested) return;
        fields->cancel_requested = true;
        if (fields->worker.is_some() && fields->key.is_some()) {
            worker = Some(fields->worker->clone());
            key    = fields->key;
        }
    }
    if (worker.is_some() && key.is_some()) {
        auto command = PollCommand::cancel_operation(*key, make_io_operation_owner(state));
        if (worker->submit_poll(rstd::move(command)).is_err()) {
            handle_io_operation_event(
                state,
                PollEventData::backend_error(*key,
                                             io::error::Error::from_kind(io::error::ErrorKind {
                                                 io::error::ErrorKind::NotConnected })));
        }
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

auto submit_io_operation(const IoOperationArc& state, FacilityCompletionToken token)
    -> FacilityCompletionSubmitResult {
    if (CURRENT_RUNTIME == nullptr || ! has_current_runtime_worker()) {
        return FacilityCompletionSubmitResult::rejected(rstd::move(token));
    }

    auto current = CURRENT_RUNTIME->current_poll_worker();
    if (current.is_err()) {
        return FacilityCompletionSubmitResult::rejected(rstd::move(token));
    }
    auto worker = state->bind_worker(rstd::move(current).unwrap_unchecked());
    if (! worker.poll_capabilities().contains(PollCapability::Completion)) {
        return FacilityCompletionSubmitResult::unsupported(rstd::move(token));
    }

    auto key      = worker.allocate_poll_key(PollKeyKind::Operation);
    auto identity = token.token();
    auto command =
        PollCommand::submit_operation(key, state->poll_operation(), make_io_operation_owner(state));
    {
        auto fields    = state->fields.lock().unwrap_unchecked();
        fields->worker = Some(worker.clone());
        fields->key    = Some(key);
        fields->token  = Some(rstd::move(token));
    }

    auto submitted = worker.submit_poll(rstd::move(command));
    if (submitted.is_err()) {
        auto returned = Option<FacilityCompletionToken> {};
        {
            auto fields    = state->fields.lock().unwrap_unchecked();
            returned       = fields->token.take();
            fields->worker = None<WorkerHandle>();
            fields->key    = None<PollKey>();
        }
        return FacilityCompletionSubmitResult::rejected(rstd::move(returned).unwrap_unchecked());
    }

    return FacilityCompletionSubmitResult::accepted(
        make_io_operation_cancellation(state, identity));
}

export class IoOperation {
    IoOperationArc                   m_state;
    Option<io::Result<IoCompletion>> m_result {};
    bool                             m_submitted { false };
    bool                             m_completed { false };

    explicit IoOperation(IoOperationArc state): m_state(rstd::move(state)) {}

    auto accept_poll_event(PollEventData poll_event) -> bool {
        if (poll_event.kind() == PollEventKind::Completion) {
            auto completion = poll_event.take_completion();
            if (completion.is_error()) {
                m_result = Some(Output(Err(completion.take_error())));
            } else {
                m_result = Some(m_state->finish(rstd::move(completion)));
            }
            return true;
        }
        if (poll_event.kind() == PollEventKind::BackendError) {
            auto error = poll_event.has_backend_error()
                             ? poll_event.take_backend_error()
                             : io::error::Error::from_kind(
                                   io::error::ErrorKind { io::error::ErrorKind::Other });
            m_result   = Some(Output(Err(rstd::move(error))));
            return true;
        }
        return false;
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
    using Output = io::Result<IoCompletion>;

    IoOperation(const IoOperation&)                    = delete;
    auto operator=(const IoOperation&) -> IoOperation& = delete;
    IoOperation(IoOperation&&) noexcept                = default;
    auto operator=(IoOperation&& other) noexcept -> IoOperation& {
        if (this != &other) {
            cancel();
            m_state     = rstd::move(other.m_state);
            m_result    = rstd::move(other.m_result);
            m_submitted = rstd::exchange(other.m_submitted, false);
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
        if (m_result.is_some()) {
            m_submitted = false;
            m_completed = true;
            return AwaitTransition::continue_();
        }
        if (CURRENT_RUNTIME == nullptr) {
            rstd::panic { "async::IoOperation advanced without an async runtime" };
        }
        if (! CURRENT_RUNTIME->io_enabled()) {
            m_result    = Some(Output(Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::Unsupported }))));
            m_completed = true;
            return AwaitTransition::continue_();
        }
        if (m_submitted) return AwaitTransition::suspend();
        m_submitted = true;
        return AwaitTransition::submit_completion(IO_COMPLETION_FACILITY_ID);
    }

    auto submit_completion(FacilityCompletionToken token) -> FacilityCompletionSubmitResult {
        if (! m_submitted || m_completed || m_result.is_some()) {
            return FacilityCompletionSubmitResult::rejected(rstd::move(token));
        }
        return submit_io_operation(m_state, rstd::move(token));
    }

    auto complete_facility(FacilityEvent& event) -> bool {
        if (! m_submitted || m_completed || m_result.is_some()) return false;
        if (! event.has_poll_event()) {
            m_result = Some(Output(Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::NotConnected }))));
            return true;
        }

        return accept_poll_event(event.take_poll_event());
    }

    auto take_output() -> Output {
        if (! m_completed || m_result.is_none()) {
            rstd::panic { "async::IoOperation output taken before completion" };
        }
        return rstd::move(m_result).unwrap_unchecked();
    }

    auto poll(mut_ref<IoOperation> self, task::Context& cx) -> task::Poll<Output> {
        auto& operation = *self;
        if (operation.m_completed) {
            rstd::panic { "async::IoOperation polled after completion" };
        }

        auto event = Option<PollEventData> {};
        {
            auto fields = operation.m_state->fields.lock().unwrap_unchecked();
            event       = fields->event.take();
            if (event.is_none() && operation.m_submitted) {
                fields->waker = Some(cx.waker().clone());
            }
        }
        if (event.is_some()) {
            if (! operation.accept_poll_event(rstd::move(event).unwrap_unchecked())) {
                rstd::panic { "async::IoOperation received an invalid Poll event" };
            }
            operation.m_completed = true;
            return task::Poll<Output>::Ready(rstd::move(operation.m_result).unwrap_unchecked());
        }

        auto* runtime = CURRENT_RUNTIME;
        if (runtime == nullptr) {
            rstd::panic { "async::IoOperation polled without an async runtime" };
        }
        if (! runtime->io_enabled()) {
            operation.m_completed = true;
            return task::Poll<Output>::Ready(Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::Unsupported })));
        }
        if (operation.m_submitted) return task::Poll<Output>::Pending();
        if (! has_current_runtime_worker()) {
            operation.m_completed = true;
            return task::Poll<Output>::Ready(Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::NotConnected })));
        }

        auto current = runtime->current_poll_worker();
        if (current.is_err()) {
            operation.m_completed = true;
            return task::Poll<Output>::Ready(Err(rstd::move(current).unwrap_err_unchecked()));
        }
        auto worker = operation.m_state->bind_worker(rstd::move(current).unwrap_unchecked());
        if (! worker.poll_capabilities().contains(PollCapability::Completion)) {
            operation.m_completed = true;
            return task::Poll<Output>::Ready(Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::Unsupported })));
        }

        auto key     = worker.allocate_poll_key(PollKeyKind::Operation);
        auto command = PollCommand::submit_operation(
            key, operation.m_state->poll_operation(), make_io_operation_owner(operation.m_state));
        {
            auto fields    = operation.m_state->fields.lock().unwrap_unchecked();
            fields->worker = Some(worker.clone());
            fields->key    = Some(key);
            fields->waker  = Some(cx.waker().clone());
        }
        operation.m_submitted = true;
        if (worker.submit_poll(rstd::move(command)).is_err()) {
            {
                auto fields    = operation.m_state->fields.lock().unwrap_unchecked();
                fields->worker = None<WorkerHandle>();
                fields->key    = None<PollKey>();
                fields->waker  = None<task::Waker>();
            }
            operation.m_completed = true;
            return task::Poll<Output>::Ready(Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::NotConnected })));
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

} // namespace rstd::async

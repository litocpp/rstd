module;
#include <rstd/macro.hpp>

export module rstd:async.reactor;
export import :async.forward;
export import :async.readiness;
export import :io.error;
export import :time;
import :async.awaitable;
import :async.poll;
import :async.runtime_core;
import :sys.fd;
import :sync;
import rstd.alloc;

using namespace rstd;
using ::alloc::vec::Vec;

namespace rstd::async
{

export struct ReadyEvent {
    Ready m_ready {};
    usize m_tick {};

    constexpr auto ready() const noexcept -> Ready { return m_ready; }
    constexpr auto tick() const noexcept -> usize { return m_tick; }

    constexpr auto is_readable() const noexcept -> bool { return m_ready.is_readable(); }
    constexpr auto is_writable() const noexcept -> bool { return m_ready.is_writable(); }
    constexpr auto is_read_closed() const noexcept -> bool { return m_ready.is_read_closed(); }
    constexpr auto is_write_closed() const noexcept -> bool { return m_ready.is_write_closed(); }
    constexpr auto is_error() const noexcept -> bool { return m_ready.is_error(); }
};

inline constexpr usize READINESS_FACILITY_ID { rstd::numeric_limits<usize>::max() - 1 };

struct ReadinessFacilityWaiter {
    usize                   id;
    Interest                interest;
    FacilityCompletionToken token;

    ReadinessFacilityWaiter(usize id, Interest interest, FacilityCompletionToken token)
        : id(id), interest(interest), token(rstd::move(token)) {}
};

struct RegistrationFields {
    Ready                        ready {};
    usize                        tick { 1 };
    Option<task::Waker>          read_waker {};
    Option<task::Waker>          write_waker {};
    usize                        read_waiter_id {};
    usize                        write_waiter_id {};
    usize                        next_waiter_id { 1 };
    Option<WorkerHandle>         worker {};
    Option<PollKey>              key {};
    Option<io::Error>            error {};
    bool                         closed { false };
    Vec<ReadinessFacilityWaiter> facility_waiters;

    RegistrationFields(): facility_waiters(Vec<ReadinessFacilityWaiter>::make()) {}
};

struct RegistrationState {
    sys::fd::RawFd                  fd;
    sync::Mutex<RegistrationFields> fields;

    explicit RegistrationState(sys::fd::RawFd fd): fd(fd), fields(RegistrationFields {}) {}
};

struct TimerFields {
    WorkerHandle        worker;
    PollKey             key;
    Option<task::Waker> waker;
    Option<io::Error>   error {};
    bool                active { true };

    TimerFields(WorkerHandle worker, PollKey key, task::Waker waker)
        : worker(rstd::move(worker)), key(key), waker(Some(rstd::move(waker))) {}
};

struct TimerState {
    sync::Mutex<TimerFields> fields;

    TimerState(WorkerHandle worker, PollKey key, task::Waker waker)
        : fields(TimerFields { rstd::move(worker), key, rstd::move(waker) }) {}
};

using RegistrationArc = sync::Arc<RegistrationState>;
using TimerArc        = sync::Arc<TimerState>;

auto make_registration_owner(const RegistrationArc& state) -> PollEventOwner;
auto make_timer_owner(const TimerArc& state) -> PollEventOwner;

auto registration_interest(const RegistrationFields& fields) noexcept -> Interest {
    auto interest = Interest {};
    if (fields.read_waker.is_some()) interest = interest | Interest::readable();
    if (fields.write_waker.is_some()) interest = interest | Interest::writable();
    for (usize i = 0; i < fields.facility_waiters.len(); ++i) {
        interest = interest | fields.facility_waiters[i].interest;
    }
    return interest;
}

void wake_all(Vec<task::Waker>& wakers) {
    while (! wakers.is_empty()) {
        rstd::move(wakers.pop().unwrap_unchecked()).wake();
    }
}

void fail_registration(const RegistrationArc& state, io::Error error) {
    auto wakers           = Vec<task::Waker>::make();
    auto tokens           = Vec<FacilityCompletionToken>::make();
    auto key              = PollKey {};
    auto completion_error = io::Error { error };
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->closed) return;
        fields->closed = true;
        fields->error  = Some(rstd::move(error));
        if (fields->read_waker.is_some()) {
            wakers.push(rstd::move(fields->read_waker).unwrap_unchecked());
        }
        if (fields->write_waker.is_some()) {
            wakers.push(rstd::move(fields->write_waker).unwrap_unchecked());
        }
        fields->read_waiter_id  = 0;
        fields->write_waiter_id = 0;
        if (fields->key.is_some()) {
            key = *fields->key;
        }
        while (! fields->facility_waiters.is_empty()) {
            tokens.push(rstd::move(fields->facility_waiters.pop()).unwrap_unchecked().token);
        }
    }
    wake_all(wakers);
    while (! tokens.is_empty()) {
        auto token = rstd::move(tokens.pop()).unwrap_unchecked();
        (void)token.complete_poll(
            PollEventData::backend_error(key, io::Error { completion_error }));
    }
}

auto submit_registration_command(const RegistrationArc& state,
                                 WorkerHandle&          worker,
                                 PollCommand            command) -> bool {
    auto submitted = worker.submit_poll(rstd::move(command));
    if (submitted.is_ok()) return true;
    fail_registration(state, io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
    return false;
}

struct ReadinessCancellationState {
    RegistrationArc registration;
    usize           waiter_id;

    ReadinessCancellationState(RegistrationArc registration, usize waiter_id)
        : registration(rstd::move(registration)), waiter_id(waiter_id) {}
};

using ReadinessCancellationArc = sync::Arc<ReadinessCancellationState>;

void cancel_readiness_waiter(const ReadinessCancellationArc& cancellation) {
    auto  token   = Option<FacilityCompletionToken> {};
    auto  worker  = Option<WorkerHandle> {};
    auto  command = Option<PollCommand> {};
    auto& state   = cancellation->registration;
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        for (usize i = 0; i < fields->facility_waiters.len(); ++i) {
            if (fields->facility_waiters[i].id != cancellation->waiter_id) {
                continue;
            }
            token = Some(rstd::move(fields->facility_waiters.remove(i)).token);
            if (! fields->closed && fields->worker.is_some() && fields->key.is_some()) {
                worker  = Some(fields->worker->clone());
                command = Some(PollCommand::update_interest(
                    *fields->key, registration_interest(*fields), make_registration_owner(state)));
            }
            break;
        }
    }
    if (command.is_some()) {
        (void)submit_registration_command(state, *worker, rstd::move(command).unwrap_unchecked());
    }
}

void readiness_cancellation_cancel(voidp data) {
    auto cancellation = ReadinessCancellationArc::from_raw(
        ::alloc::sync::ArcRaw<ReadinessCancellationState>::from_raw(data));
    cancel_readiness_waiter(cancellation);
}

void readiness_cancellation_drop(voidp data) {
    auto cancellation = ReadinessCancellationArc::from_raw(
        ::alloc::sync::ArcRaw<ReadinessCancellationState>::from_raw(data));
    (void)cancellation;
}

const RawFacilityCancellationVTable READINESS_CANCELLATION_VTABLE {
    &readiness_cancellation_cancel,
    &readiness_cancellation_drop,
};

auto make_readiness_cancellation(const RegistrationArc& state, usize waiter_id, FacilityToken token)
    -> FacilityCancellation {
    auto cancellation = ReadinessCancellationArc::make(state.clone(), waiter_id);
    return FacilityCancellation::from_raw(
        token,
        RawFacilityCancellation::from_raw_parts(rstd::move(cancellation).into_raw().into_raw(),
                                                rstd::addressof(READINESS_CANCELLATION_VTABLE)));
}

void handle_registration_event(const RegistrationArc& state, PollEventData data) {
    if (data.kind() == PollEventKind::BackendError) {
        auto error = data.has_backend_error()
                         ? data.take_backend_error()
                         : io::Error::from_kind(io::ErrorKind { io::ErrorKind::Other });
        fail_registration(state, rstd::move(error));
        return;
    }
    if (data.kind() != PollEventKind::Readiness) return;

    auto ready = data.readiness();
    auto key   = data.key();

    auto wakers  = Vec<task::Waker>::make();
    auto tokens  = Vec<FacilityCompletionToken>::make();
    auto worker  = Option<WorkerHandle> {};
    auto command = Option<PollCommand> {};
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->closed) return;

        fields->ready |= ready;
        ++fields->tick;
        if ((ready.is_readable() || ready.is_read_closed() || ready.is_error()) &&
            fields->read_waker.is_some()) {
            wakers.push(rstd::move(fields->read_waker).unwrap_unchecked());
            fields->read_waiter_id = 0;
        }
        if ((ready.is_writable() || ready.is_write_closed() || ready.is_error()) &&
            fields->write_waker.is_some()) {
            wakers.push(rstd::move(fields->write_waker).unwrap_unchecked());
            fields->write_waiter_id = 0;
        }
        for (usize i = 0; i < fields->facility_waiters.len();) {
            if (ready.for_interest(fields->facility_waiters[i].interest).is_empty()) {
                ++i;
                continue;
            }
            tokens.push(rstd::move(fields->facility_waiters.remove(i)).token);
        }

        if (fields->worker.is_some() && fields->key.is_some()) {
            worker  = Some(fields->worker->clone());
            command = Some(PollCommand::update_interest(
                *fields->key, registration_interest(*fields), make_registration_owner(state)));
        }
    }

    if (command.is_some()) {
        (void)submit_registration_command(state, *worker, rstd::move(command).unwrap_unchecked());
    }
    wake_all(wakers);
    while (! tokens.is_empty()) {
        auto token = rstd::move(tokens.pop()).unwrap_unchecked();
        (void)token.complete_poll(PollEventData::readiness(key, ready));
    }
}

auto registration_owner_clone(voidp data) -> RawPollEventOwner;

void registration_owner_dispatch(voidp data, PollEventData event) {
    auto state =
        RegistrationArc::from_raw(::alloc::sync::ArcRaw<RegistrationState>::from_raw(data));
    handle_registration_event(state, rstd::move(event));
    (void)rstd::move(state).into_raw().into_raw();
}

void registration_owner_drop(voidp data) {
    auto state =
        RegistrationArc::from_raw(::alloc::sync::ArcRaw<RegistrationState>::from_raw(data));
    (void)state;
}

extern const RawPollEventOwnerVTable REGISTRATION_OWNER_VTABLE;

const RawPollEventOwnerVTable REGISTRATION_OWNER_VTABLE {
    &registration_owner_clone,
    &registration_owner_dispatch,
    &registration_owner_drop,
};

auto make_registration_owner(const RegistrationArc& state) -> PollEventOwner {
    auto owned = state.clone();
    return PollEventOwner::from_raw(RawPollEventOwner::from_raw_parts(
        rstd::move(owned).into_raw().into_raw(), rstd::addressof(REGISTRATION_OWNER_VTABLE)));
}

auto registration_owner_clone(voidp data) -> RawPollEventOwner {
    auto state =
        RegistrationArc::from_raw(::alloc::sync::ArcRaw<RegistrationState>::from_raw(data));
    auto cloned = state.clone();
    (void)rstd::move(state).into_raw().into_raw();
    return RawPollEventOwner::from_raw_parts(rstd::move(cloned).into_raw().into_raw(),
                                             rstd::addressof(REGISTRATION_OWNER_VTABLE));
}

void handle_timer_event(const TimerArc& state, PollEventData data) {
    auto waker = Option<task::Waker> {};
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (! fields->active) return;
        if (data.kind() == PollEventKind::BackendError) {
            fields->error =
                data.has_backend_error()
                    ? Some(data.take_backend_error())
                    : Some(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Other }));
        } else if (data.kind() != PollEventKind::Timer) {
            return;
        }
        fields->active = false;
        waker          = fields->waker.take();
    }
    if (waker.is_some()) {
        rstd::move(waker).unwrap_unchecked().wake();
    }
}

auto timer_owner_clone(voidp data) -> RawPollEventOwner;

void timer_owner_dispatch(voidp data, PollEventData event) {
    auto state = TimerArc::from_raw(::alloc::sync::ArcRaw<TimerState>::from_raw(data));
    handle_timer_event(state, rstd::move(event));
    (void)rstd::move(state).into_raw().into_raw();
}

void timer_owner_drop(voidp data) {
    auto state = TimerArc::from_raw(::alloc::sync::ArcRaw<TimerState>::from_raw(data));
    (void)state;
}

const RawPollEventOwnerVTable TIMER_OWNER_VTABLE {
    &timer_owner_clone,
    &timer_owner_dispatch,
    &timer_owner_drop,
};

auto timer_owner_clone(voidp data) -> RawPollEventOwner {
    auto state  = TimerArc::from_raw(::alloc::sync::ArcRaw<TimerState>::from_raw(data));
    auto cloned = state.clone();
    (void)rstd::move(state).into_raw().into_raw();
    return RawPollEventOwner::from_raw_parts(rstd::move(cloned).into_raw().into_raw(),
                                             rstd::addressof(TIMER_OWNER_VTABLE));
}

auto make_timer_owner(const TimerArc& state) -> PollEventOwner {
    auto owned = state.clone();
    return PollEventOwner::from_raw(RawPollEventOwner::from_raw_parts(
        rstd::move(owned).into_raw().into_raw(), rstd::addressof(TIMER_OWNER_VTABLE)));
}

auto try_registration_readiness(const RegistrationArc& state, Interest interest)
    -> Option<io::Result<ReadyEvent>> {
    auto fields = state->fields.lock().unwrap_unchecked();
    auto ready  = fields->ready.for_interest(interest);
    if (! ready.is_empty()) {
        return Some(io::Result<ReadyEvent>(Ok(ReadyEvent { ready, fields->tick })));
    }
    if (fields->closed) {
        auto error = fields->error.is_some()
                         ? io::Error { *fields->error }
                         : io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected });
        return Some(io::Result<ReadyEvent>(Err(rstd::move(error))));
    }
    return None();
}

auto registration_ready_event(const RegistrationArc& state, Interest interest, Ready ready)
    -> ReadyEvent {
    auto fields = state->fields.lock().unwrap_unchecked();
    return ReadyEvent { ready.for_interest(interest), fields->tick };
}

auto submit_registration_readiness(const RegistrationArc&  state,
                                   FacilityCompletionToken token,
                                   Interest                interest,
                                   usize& waiter_id) -> FacilityCompletionSubmitResult {
    auto identity        = token.token();
    auto worker          = Option<WorkerHandle> {};
    auto command         = Option<PollCommand> {};
    bool register_source = false;
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->closed) {
            return FacilityCompletionSubmitResult::rejected(rstd::move(token));
        }
        if (waiter_id == 0) {
            waiter_id = fields->next_waiter_id++;
        }
        for (usize i = 0; i < fields->facility_waiters.len(); ++i) {
            if (fields->facility_waiters[i].id == waiter_id) {
                return FacilityCompletionSubmitResult::rejected(rstd::move(token));
            }
        }

        if (fields->worker.is_some() && fields->key.is_some()) {
            worker = Some(fields->worker->clone());
        } else if (CURRENT_RUNTIME != nullptr && has_current_runtime_worker()) {
            auto current = CURRENT_RUNTIME->current_poll_worker();
            if (current.is_err()) {
                return FacilityCompletionSubmitResult::rejected(rstd::move(token));
            }
            auto bound      = rstd::move(current).unwrap_unchecked();
            auto key        = bound.allocate_poll_key(PollKeyKind::Registration);
            fields->worker  = Some(bound.clone());
            fields->key     = Some(key);
            worker          = Some(rstd::move(bound));
            register_source = true;
        } else {
            return FacilityCompletionSubmitResult::rejected(rstd::move(token));
        }

        fields->facility_waiters.push(
            ReadinessFacilityWaiter { waiter_id, interest, rstd::move(token) });
        if (fields->key.is_none()) {
            rstd::panic { "async readiness facility bound without a Poll key" };
        }
        auto key = *fields->key;
        if (register_source) {
            command = Some(PollCommand::register_source(
                key, state->fd, registration_interest(*fields), make_registration_owner(state)));
        } else {
            command = Some(PollCommand::update_interest(
                key, registration_interest(*fields), make_registration_owner(state)));
        }
    }

    (void)submit_registration_command(state, *worker, rstd::move(command).unwrap_unchecked());
    return FacilityCompletionSubmitResult::accepted(
        make_readiness_cancellation(state, waiter_id, identity));
}

auto poll_registration_readiness(const RegistrationArc& state,
                                 task::Context&         cx,
                                 Interest               interest,
                                 usize& waiter_id) -> task::Poll<io::Result<ReadyEvent>> {
    if (CURRENT_RUNTIME != nullptr && ! CURRENT_RUNTIME->io_enabled()) {
        return task::Poll<io::Result<ReadyEvent>>::Ready(
            Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported })));
    }
    if (interest.is_empty()) {
        return task::Poll<io::Result<ReadyEvent>>::Ready(Ok(ReadyEvent {}));
    }

    auto worker  = Option<WorkerHandle> {};
    auto command = Option<PollCommand> {};
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        auto ready  = fields->ready.for_interest(interest);
        if (! ready.is_empty()) {
            return task::Poll<io::Result<ReadyEvent>>::Ready(
                Ok(ReadyEvent { ready, fields->tick }));
        }
        if (fields->closed) {
            auto error = fields->error.is_some()
                             ? io::Error { *fields->error }
                             : io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected });
            return task::Poll<io::Result<ReadyEvent>>::Ready(Err(rstd::move(error)));
        }

        if (waiter_id == 0) waiter_id = fields->next_waiter_id++;
        if (interest.is_readable()) {
            fields->read_waker     = Some(cx.waker().clone());
            fields->read_waiter_id = waiter_id;
        }
        if (interest.is_writable()) {
            fields->write_waker     = Some(cx.waker().clone());
            fields->write_waiter_id = waiter_id;
        }

        if (fields->worker.is_some() && fields->key.is_some()) {
            worker  = Some(fields->worker->clone());
            command = Some(PollCommand::update_interest(
                *fields->key, registration_interest(*fields), make_registration_owner(state)));
        } else if (CURRENT_RUNTIME != nullptr && has_current_runtime_worker()) {
            auto current = CURRENT_RUNTIME->current_poll_worker();
            if (current.is_err()) {
                return task::Poll<io::Result<ReadyEvent>>::Ready(
                    Err(rstd::move(current).unwrap_err_unchecked()));
            }
            auto bound     = rstd::move(current).unwrap_unchecked();
            auto key       = bound.allocate_poll_key(PollKeyKind::Registration);
            fields->worker = Some(bound.clone());
            fields->key    = Some(key);
            worker         = Some(rstd::move(bound));
            command        = Some(PollCommand::register_source(
                key, state->fd, registration_interest(*fields), make_registration_owner(state)));
        }
    }

    if (command.is_some() &&
        ! submit_registration_command(state, *worker, rstd::move(command).unwrap_unchecked())) {
        return task::Poll<io::Result<ReadyEvent>>::Ready(
            Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected })));
    }
    return task::Poll<io::Result<ReadyEvent>>::Pending();
}

void clear_registration_waker(const RegistrationArc& state, Interest interest, usize waiter_id) {
    if (! state || waiter_id == 0) return;

    auto worker  = Option<WorkerHandle> {};
    auto command = Option<PollCommand> {};
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (interest.is_readable() && fields->read_waiter_id == waiter_id) {
            fields->read_waker     = None();
            fields->read_waiter_id = 0;
        }
        if (interest.is_writable() && fields->write_waiter_id == waiter_id) {
            fields->write_waker     = None();
            fields->write_waiter_id = 0;
        }
        if (! fields->closed && fields->worker.is_some() && fields->key.is_some()) {
            worker  = Some(fields->worker->clone());
            command = Some(PollCommand::update_interest(
                *fields->key, registration_interest(*fields), make_registration_owner(state)));
        }
    }
    if (command.is_some()) {
        (void)submit_registration_command(state, *worker, rstd::move(command).unwrap_unchecked());
    }
}

export class Registration {
    RegistrationArc m_state;

    explicit Registration(RegistrationArc state): m_state(rstd::move(state)) {}

    friend class ReadinessFuture;

public:
    Registration(const Registration&)                    = delete;
    auto operator=(const Registration&) -> Registration& = delete;

    Registration(Registration&& other) noexcept: m_state(rstd::move(other.m_state)) {}

    auto operator=(Registration&& other) noexcept -> Registration& {
        if (this != &other) {
            reset();
            m_state = rstd::move(other.m_state);
        }
        return *this;
    }

    ~Registration() { reset(); }

    static auto register_fd(sys::fd::RawFd fd) -> io::Result<Registration> {
        if (fd == sys::fd::INVALID_RAW_FD) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
        }
        return Ok(Registration { RegistrationArc::make(fd) });
    }

    void reset() {
        if (! m_state) return;

        auto wakers  = Vec<task::Waker>::make();
        auto tokens  = Vec<FacilityCompletionToken>::make();
        auto worker  = Option<WorkerHandle> {};
        auto command = Option<PollCommand> {};
        {
            auto fields = m_state->fields.lock().unwrap_unchecked();
            if (! fields->closed) {
                fields->closed = true;
                if (fields->read_waker.is_some()) {
                    wakers.push(rstd::move(fields->read_waker).unwrap_unchecked());
                }
                if (fields->write_waker.is_some()) {
                    wakers.push(rstd::move(fields->write_waker).unwrap_unchecked());
                }
                fields->read_waiter_id  = 0;
                fields->write_waiter_id = 0;
                while (! fields->facility_waiters.is_empty()) {
                    tokens.push(
                        rstd::move(fields->facility_waiters.pop()).unwrap_unchecked().token);
                }
                if (fields->worker.is_some() && fields->key.is_some()) {
                    worker  = Some(fields->worker->clone());
                    command = Some(PollCommand::deregister_source(
                        *fields->key, make_registration_owner(m_state)));
                }
            }
        }

        if (command.is_some()) {
            (void)worker->submit_poll(rstd::move(command).unwrap_unchecked());
        }
        wake_all(wakers);
        while (! tokens.is_empty()) {
            auto token = rstd::move(tokens.pop()).unwrap_unchecked();
            (void)token.complete(FacilityEventKind::Canceled);
        }
        m_state.reset();
    }

    auto poll_readiness(task::Context& cx, Interest interest, usize& waiter_id)
        -> task::Poll<io::Result<ReadyEvent>> {
        return poll_registration_readiness(m_state, cx, interest, waiter_id);
    }

    void clear_readiness(Ready ready) {
        if (! m_state) return;
        auto fields = m_state->fields.lock().unwrap_unchecked();
        fields->ready.remove(ready);
    }

    void clear_readiness(ReadyEvent event) {
        if (! m_state) return;
        auto fields = m_state->fields.lock().unwrap_unchecked();
        if (fields->tick == event.tick()) {
            fields->ready.remove(event.ready());
        }
    }

    void clear_waker(Interest interest, usize waiter_id) {
        clear_registration_waker(m_state, interest, waiter_id);
    }
};

export class TimerRegistration {
    TimerArc m_state;

    explicit TimerRegistration(TimerArc state): m_state(rstd::move(state)) {}

public:
    TimerRegistration(const TimerRegistration&)                    = delete;
    auto operator=(const TimerRegistration&) -> TimerRegistration& = delete;
    TimerRegistration(TimerRegistration&&) noexcept                = default;

    auto operator=(TimerRegistration&& other) noexcept -> TimerRegistration& {
        if (this != &other) {
            reset();
            m_state = rstd::move(other.m_state);
        }
        return *this;
    }

    ~TimerRegistration() { reset(); }

    static auto register_deadline(time::Instant deadline, task::Waker waker)
        -> io::Result<TimerRegistration> {
        if (CURRENT_RUNTIME == nullptr || ! CURRENT_RUNTIME->time_enabled()) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
        }
        auto current = CURRENT_RUNTIME->current_poll_worker();
        if (current.is_err()) return Err(rstd::move(current).unwrap_err_unchecked());

        auto worker    = rstd::move(current).unwrap_unchecked();
        auto key       = worker.allocate_poll_key(PollKeyKind::Timer);
        auto state     = TimerArc::make(worker.clone(), key, rstd::move(waker));
        auto command   = PollCommand::arm_timer(key, deadline, make_timer_owner(state));
        auto submitted = worker.submit_poll(rstd::move(command));
        if (submitted.is_err()) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        }
        return Ok(TimerRegistration { rstd::move(state) });
    }

    auto update_waker(task::Waker waker) -> io::Result<empty> {
        if (! m_state) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
        }
        auto fields = m_state->fields.lock().unwrap_unchecked();
        if (fields->error.is_some()) return Err(io::Error { *fields->error });
        if (! fields->active) {
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        }
        fields->waker = Some(rstd::move(waker));
        return Ok(empty {});
    }

    void reset() {
        if (! m_state) return;

        auto worker  = Option<WorkerHandle> {};
        auto command = Option<PollCommand> {};
        {
            auto fields = m_state->fields.lock().unwrap_unchecked();
            if (fields->active) {
                fields->active = false;
                fields->waker  = None();
                worker         = Some(fields->worker.clone());
                command = Some(PollCommand::cancel_timer(fields->key, make_timer_owner(m_state)));
            }
        }
        if (command.is_some()) {
            (void)worker->submit_poll(rstd::move(command).unwrap_unchecked());
        }
        m_state.reset();
    }
};

export class ReadinessFuture {
    RegistrationArc                m_state;
    Interest                       m_interest {};
    usize                          m_waiter_id {};
    bool                           m_started { false };
    Option<io::Result<ReadyEvent>> m_facility_result;
    bool                           m_facility_submitted { false };
    bool                           m_completed { false };

public:
    using Output = io::Result<ReadyEvent>;

    ReadinessFuture(Registration& registration, Interest interest)
        : m_state(registration.m_state.clone()), m_interest(interest), m_facility_result(None()) {}

    ReadinessFuture(const ReadinessFuture&)                    = delete;
    auto operator=(const ReadinessFuture&) -> ReadinessFuture& = delete;

    ReadinessFuture(ReadinessFuture&& other) noexcept
        : m_state(rstd::move(other.m_state)),
          m_interest(other.m_interest),
          m_waiter_id(rstd::exchange(other.m_waiter_id, 0)),
          m_started(rstd::exchange(other.m_started, false)),
          m_facility_result(other.m_facility_result.take()),
          m_facility_submitted(rstd::exchange(other.m_facility_submitted, false)),
          m_completed(rstd::exchange(other.m_completed, false)) {}

    auto operator=(ReadinessFuture&& other) noexcept -> ReadinessFuture& {
        if (this != &other) {
            cancel();
            m_state              = rstd::move(other.m_state);
            m_interest           = other.m_interest;
            m_waiter_id          = rstd::exchange(other.m_waiter_id, 0);
            m_started            = rstd::exchange(other.m_started, false);
            m_facility_result    = other.m_facility_result.take();
            m_facility_submitted = rstd::exchange(other.m_facility_submitted, false);
            m_completed          = rstd::exchange(other.m_completed, false);
        }
        return *this;
    }

    ~ReadinessFuture() { cancel(); }

    auto poll(mut_ref<ReadinessFuture> self, task::Context& cx) -> task::Poll<Output> {
        auto& future = *self;
        if (future.m_facility_submitted || future.m_completed) {
            rstd::panic { "async::ReadinessFuture polled after direct await started" };
        }
        future.m_started = true;
        auto result =
            poll_registration_readiness(future.m_state, cx, future.m_interest, future.m_waiter_id);
        if (result.is_ready()) {
            future.m_waiter_id = 0;
            future.m_started   = false;
        }
        return result;
    }

    auto advance(AwaitContext& cx) -> AwaitTransition {
        if (cx.execution_domain() == ExecutionDomainKind::ExternalExecutor) {
            return AwaitTransition::return_to_owner();
        }
        if (m_completed) {
            rstd::panic { "async::ReadinessFuture advanced after completion" };
        }
        auto* runtime = CURRENT_RUNTIME;
        if (runtime == nullptr) {
            rstd::panic { "async::ReadinessFuture advanced without an async runtime" };
        }
        if (m_facility_result.is_some()) {
            m_facility_submitted = false;
            m_completed          = true;
            return AwaitTransition::continue_();
        }
        if (! runtime->io_enabled()) {
            m_facility_result.insert(
                Output(Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }))));
            m_completed = true;
            return AwaitTransition::continue_();
        }
        if (m_interest.is_empty()) {
            m_facility_result.insert(Output(Ok(ReadyEvent {})));
            m_completed = true;
            return AwaitTransition::continue_();
        }
        auto immediate = try_registration_readiness(m_state, m_interest);
        if (immediate.is_some()) {
            m_facility_result = Some(rstd::move(immediate).unwrap_unchecked());
            m_completed       = true;
            return AwaitTransition::continue_();
        }
        if (m_facility_submitted) {
            return AwaitTransition::suspend();
        }
        m_facility_submitted = true;
        return AwaitTransition::submit_completion(READINESS_FACILITY_ID);
    }

    auto submit_completion(FacilityCompletionToken token) -> FacilityCompletionSubmitResult {
        if (! m_facility_submitted || m_completed || m_facility_result.is_some()) {
            return FacilityCompletionSubmitResult::rejected(rstd::move(token));
        }
        return submit_registration_readiness(m_state, rstd::move(token), m_interest, m_waiter_id);
    }

    auto complete_facility(FacilityEvent& event) -> bool {
        if (! m_facility_submitted || m_completed || m_facility_result.is_some()) {
            return false;
        }
        if (! event.has_poll_event()) {
            m_facility_result.insert(
                Output(Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }))));
            return true;
        }

        auto poll_event = event.take_poll_event();
        if (poll_event.kind() == PollEventKind::Readiness) {
            auto ready = poll_event.readiness().for_interest(m_interest);
            if (ready.is_empty()) {
                return false;
            }
            m_facility_result.insert(
                Output(Ok(registration_ready_event(m_state, m_interest, ready))));
            return true;
        }
        if (poll_event.kind() == PollEventKind::BackendError) {
            auto error = poll_event.has_backend_error()
                             ? poll_event.take_backend_error()
                             : io::Error::from_kind(io::ErrorKind { io::ErrorKind::Other });
            m_facility_result.insert(Output(Err(rstd::move(error))));
            return true;
        }
        return false;
    }

    auto take_output() -> Output {
        if (! m_completed || m_facility_result.is_none()) {
            rstd::panic { "async::ReadinessFuture output taken before completion" };
        }
        return rstd::move(m_facility_result).unwrap_unchecked();
    }

private:
    void cancel() {
        if (m_started && m_state && m_waiter_id != 0) {
            clear_registration_waker(m_state, m_interest, m_waiter_id);
        }
        m_waiter_id = 0;
        m_started   = false;
    }
};

template<>
struct AwaitableTraits<ReadinessFuture> {
    using Output = ReadinessFuture::Output;

    static auto make_suspension(ReadinessFuture&& future) {
        return AwaitSuspension<ReadinessFuture> { rstd::move(future) };
    }
};

} // namespace rstd::async

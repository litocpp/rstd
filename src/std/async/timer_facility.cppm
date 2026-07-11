export module rstd:async.timer_facility;
import :async.poll;
import :async.runtime_core;
import :sync;
import :time;
import rstd.alloc;

using namespace rstd;

namespace rstd::async
{

export inline constexpr usize TIMER_FACILITY_ID { rstd::numeric_limits<usize>::max() };

struct TimerFacilityFields {
    WorkerHandle                    worker;
    PollKey                         key;
    Option<FacilityCompletionToken> token;
    bool                            active { true };

    TimerFacilityFields(WorkerHandle worker, PollKey key, FacilityCompletionToken token)
        : worker(rstd::move(worker)), key(key), token(Some(rstd::move(token))) {}
};

struct TimerFacilityState {
    sync::Mutex<TimerFacilityFields> fields;

    TimerFacilityState(WorkerHandle worker, PollKey key, FacilityCompletionToken token)
        : fields(TimerFacilityFields { rstd::move(worker), key, rstd::move(token) }) {}
};

using TimerFacilityArc = sync::Arc<TimerFacilityState>;

auto timer_facility_owner_clone(voidp data) -> RawPollEventOwner;

void timer_facility_owner_dispatch(voidp data, PollEventData event) {
    auto state =
        TimerFacilityArc::from_raw(::alloc::sync::ArcRaw<TimerFacilityState>::from_raw(data));
    auto token = Option<FacilityCompletionToken> {};
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (fields->active &&
            (event.kind() == PollEventKind::Timer || event.kind() == PollEventKind::BackendError)) {
            fields->active = false;
            token          = fields->token.take();
        }
    }
    if (token.is_some()) {
        (void)rstd::move(token).unwrap_unchecked().complete_poll(rstd::move(event));
    }
    (void)rstd::move(state).into_raw().into_raw();
}

void timer_facility_owner_drop(voidp data) {
    auto state =
        TimerFacilityArc::from_raw(::alloc::sync::ArcRaw<TimerFacilityState>::from_raw(data));
    (void)state;
}

const RawPollEventOwnerVTable TIMER_FACILITY_OWNER_VTABLE {
    &timer_facility_owner_clone,
    &timer_facility_owner_dispatch,
    &timer_facility_owner_drop,
};

auto timer_facility_owner_clone(voidp data) -> RawPollEventOwner {
    auto state =
        TimerFacilityArc::from_raw(::alloc::sync::ArcRaw<TimerFacilityState>::from_raw(data));
    auto cloned = state.clone();
    (void)rstd::move(state).into_raw().into_raw();
    return RawPollEventOwner::from_raw_parts(rstd::move(cloned).into_raw().into_raw(),
                                             rstd::addressof(TIMER_FACILITY_OWNER_VTABLE));
}

auto make_timer_facility_owner(const TimerFacilityArc& state) -> PollEventOwner {
    auto owned = state.clone();
    return PollEventOwner::from_raw(RawPollEventOwner::from_raw_parts(
        rstd::move(owned).into_raw().into_raw(), rstd::addressof(TIMER_FACILITY_OWNER_VTABLE)));
}

void cancel_timer_facility(const TimerFacilityArc& state) {
    auto token   = Option<FacilityCompletionToken> {};
    auto worker  = Option<WorkerHandle> {};
    auto command = Option<PollCommand> {};
    {
        auto fields = state->fields.lock().unwrap_unchecked();
        if (! fields->active) {
            return;
        }
        fields->active = false;
        token          = fields->token.take();
        worker         = Some(fields->worker.clone());
        command = Some(PollCommand::cancel_timer(fields->key, make_timer_facility_owner(state)));
    }
    if (command.is_some()) {
        (void)worker->submit_poll(rstd::move(command).unwrap_unchecked());
    }
}

void timer_facility_cancel(voidp data) {
    auto state =
        TimerFacilityArc::from_raw(::alloc::sync::ArcRaw<TimerFacilityState>::from_raw(data));
    cancel_timer_facility(state);
}

void timer_facility_cancel_drop(voidp data) {
    auto state =
        TimerFacilityArc::from_raw(::alloc::sync::ArcRaw<TimerFacilityState>::from_raw(data));
    (void)state;
}

const RawFacilityCancellationVTable TIMER_FACILITY_CANCELLATION_VTABLE {
    &timer_facility_cancel,
    &timer_facility_cancel_drop,
};

auto make_timer_facility_cancellation(const TimerFacilityArc& state, FacilityToken token)
    -> FacilityCancellation {
    auto owned = state.clone();
    return FacilityCancellation::from_raw(token,
                                          RawFacilityCancellation::from_raw_parts(
                                              rstd::move(owned).into_raw().into_raw(),
                                              rstd::addressof(TIMER_FACILITY_CANCELLATION_VTABLE)));
}

export class TimerFacilityRegistration {
    TimerFacilityArc m_state;
    FacilityToken    m_token;

    TimerFacilityRegistration(TimerFacilityArc state, FacilityToken token)
        : m_state(rstd::move(state)), m_token(token) {}

public:
    TimerFacilityRegistration(const TimerFacilityRegistration&)                    = delete;
    auto operator=(const TimerFacilityRegistration&) -> TimerFacilityRegistration& = delete;

    TimerFacilityRegistration(TimerFacilityRegistration&& other) noexcept
        : m_state(rstd::move(other.m_state)), m_token(other.m_token) {}

    auto operator=(TimerFacilityRegistration&& other) noexcept -> TimerFacilityRegistration& {
        if (this != &other) {
            reset();
            m_state = rstd::move(other.m_state);
            m_token = other.m_token;
        }
        return *this;
    }

    ~TimerFacilityRegistration() { reset(); }

    static auto submit(time::Instant deadline, FacilityCompletionToken token)
        -> Result<TimerFacilityRegistration, FacilityCompletionToken> {
        auto* runtime = CURRENT_RUNTIME;
        if (runtime == nullptr) {
            return Err(rstd::move(token));
        }
        auto current = runtime->current_poll_worker();
        if (current.is_err()) {
            return Err(rstd::move(token));
        }

        auto identity = token.token();
        auto worker   = rstd::move(current).unwrap_unchecked();
        auto key      = worker.allocate_poll_key(PollKeyKind::Timer);
        auto state    = TimerFacilityArc::make(worker.clone(), key, rstd::move(token));
        auto command  = PollCommand::arm_timer(key, deadline, make_timer_facility_owner(state));
        auto result   = worker.submit_poll(rstd::move(command));
        if (result.is_err()) {
            auto fields    = state->fields.lock().unwrap_unchecked();
            fields->active = false;
            return Err(fields->token.take().unwrap_unchecked());
        }
        return Ok(TimerFacilityRegistration { rstd::move(state), identity });
    }

    auto cancellation() const -> FacilityCancellation {
        return make_timer_facility_cancellation(m_state, m_token);
    }

    void reset() {
        if (! m_state) {
            return;
        }

        cancel_timer_facility(m_state);
        m_state.reset();
    }
};

} // namespace rstd::async

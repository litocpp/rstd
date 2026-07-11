export module rstd:async.coro_driver;
import :async.facility;
import :async.runtime_core;
import :async.task;

using namespace rstd;

namespace rstd::async
{

enum class CoroFrameState
{
    InitialSuspended,
    AwaitSuspended,
    FinalSuspended,
    ResultTaken,
};

enum class DriveActionKind
{
    Suspended,
    SubmitCompletion,
    SubmitFacility,
    ReturnToOwner,
    FinalSuspended,
};

class DriveAction {
    DriveActionKind  m_kind { DriveActionKind::Suspended };
    FacilityEndpoint m_endpoint;
    usize            m_facility_id { 0 };

    explicit DriveAction(DriveActionKind kind): m_kind(kind) {}

    explicit DriveAction(usize facility_id)
        : m_kind(DriveActionKind::SubmitCompletion), m_facility_id(facility_id) {}

    explicit DriveAction(FacilityEndpoint endpoint)
        : m_kind(DriveActionKind::SubmitFacility), m_endpoint(rstd::move(endpoint)) {}

public:
    static auto suspended() -> DriveAction { return DriveAction { DriveActionKind::Suspended }; }

    static auto submit_facility(FacilityEndpoint endpoint) -> DriveAction {
        return DriveAction { rstd::move(endpoint) };
    }

    static auto submit_completion(usize facility_id) -> DriveAction {
        return DriveAction { facility_id };
    }

    static auto return_to_owner() -> DriveAction {
        return DriveAction { DriveActionKind::ReturnToOwner };
    }

    static auto final_suspended() -> DriveAction {
        return DriveAction { DriveActionKind::FinalSuspended };
    }

    auto kind() const noexcept -> DriveActionKind { return m_kind; }
    auto is_suspended() const noexcept -> bool { return m_kind == DriveActionKind::Suspended; }
    auto is_submit_facility() const noexcept -> bool {
        return m_kind == DriveActionKind::SubmitFacility;
    }
    auto is_submit_completion() const noexcept -> bool {
        return m_kind == DriveActionKind::SubmitCompletion;
    }
    auto is_return_to_owner() const noexcept -> bool {
        return m_kind == DriveActionKind::ReturnToOwner;
    }
    auto is_final_suspended() const noexcept -> bool {
        return m_kind == DriveActionKind::FinalSuspended;
    }
    auto take_endpoint() -> FacilityEndpoint { return rstd::move(m_endpoint); }
    auto facility_id() const noexcept -> usize { return m_facility_id; }
};

template<typename T>
auto resume_coro(coro<T>& task, CoroFrameState& state, task::Context& cx) -> DriveAction {
    auto& handle = CoroAccess<T>::handle(task);
    if (! handle) {
        rstd::panic { "empty async coro resumed" };
    }
    if (state == CoroFrameState::ResultTaken) {
        rstd::panic { "async coro resumed after completion" };
    }
    if (state == CoroFrameState::FinalSuspended) {
        return DriveAction::final_suspended();
    }

    auto& promise = handle.promise();
    while (true) {
        auto await_context = async::AwaitContext { cx, current_execution_domain() };
        if (promise.awaiting) {
            auto awaiting   = promise.awaiting;
            auto transition = awaiting.advance(await_context);
            if (transition.kind() == async::AwaitTransitionKind::Suspend) {
                state = CoroFrameState::AwaitSuspended;
                return DriveAction::suspended();
            }
            if (transition.kind() == async::AwaitTransitionKind::SubmitFacility) {
                state = CoroFrameState::AwaitSuspended;
                return DriveAction::submit_facility(transition.take_endpoint());
            }
            if (transition.kind() == async::AwaitTransitionKind::SubmitCompletion) {
                state = CoroFrameState::AwaitSuspended;
                return DriveAction::submit_completion(transition.facility_id());
            }
            if (transition.kind() == async::AwaitTransitionKind::ReturnToOwner) {
                rstd::panic { "async await operation returned to its current runtime owner" };
            }
            promise.awaiting = {};
            handle.resume();
        } else {
            handle.resume();
        }

        if (handle.done()) {
            state = CoroFrameState::FinalSuspended;
            return DriveAction::final_suspended();
        }
    }
}

template<typename T>
auto resume_coro_external_segment(coro<T>& task, CoroFrameState& state, task::Context& cx)
    -> DriveAction {
    auto& handle = CoroAccess<T>::handle(task);
    if (! handle) {
        rstd::panic { "empty async coro resumed" };
    }
    if (state == CoroFrameState::FinalSuspended || state == CoroFrameState::ResultTaken) {
        rstd::panic { "async coro resumed after completion" };
    }

    auto& promise = handle.promise();
    while (true) {
        if (promise.awaiting) {
            auto await_context = AwaitContext { cx, ExecutionDomainKind::ExternalExecutor };
            auto awaiting      = promise.awaiting;
            auto transition    = awaiting.advance(await_context);
            if (transition.kind() == AwaitTransitionKind::SubmitFacility) {
                state = CoroFrameState::AwaitSuspended;
                return DriveAction::submit_facility(transition.take_endpoint());
            }
            if (transition.kind() == AwaitTransitionKind::SubmitCompletion ||
                transition.kind() == AwaitTransitionKind::Suspend ||
                transition.kind() == AwaitTransitionKind::ReturnToOwner) {
                state = CoroFrameState::AwaitSuspended;
                return DriveAction::return_to_owner();
            }
            promise.awaiting = {};
            handle.resume();
        } else {
            handle.resume();
        }

        if (handle.done()) {
            state = CoroFrameState::FinalSuspended;
            return DriveAction::final_suspended();
        }
    }
}

template<typename T>
auto complete_coro_facility(coro<T>& task, CoroFrameState state, FacilityEvent& event) -> bool {
    if (state != CoroFrameState::AwaitSuspended) {
        return false;
    }
    auto& promise = CoroAccess<T>::handle(task).promise();
    return promise.awaiting && promise.awaiting.complete_facility(event);
}

template<typename T>
auto submit_coro_completion(coro<T>& task, CoroFrameState state, FacilityCompletionToken token)
    -> FacilityCompletionSubmitResult {
    if (state != CoroFrameState::AwaitSuspended) {
        return FacilityCompletionSubmitResult::rejected(rstd::move(token));
    }
    auto& promise = CoroAccess<T>::handle(task).promise();
    if (! promise.awaiting) {
        return FacilityCompletionSubmitResult::rejected(rstd::move(token));
    }
    return promise.awaiting.submit_completion(rstd::move(token));
}

template<typename T>
auto take_coro_result(coro<T>& task, CoroFrameState& state) -> T {
    if (state != CoroFrameState::FinalSuspended) {
        rstd::panic { "async coro result taken before final suspend" };
    }
    state = CoroFrameState::ResultTaken;
    return CoroAccess<T>::handle(task).promise().take_result();
}

template<>
inline auto take_coro_result(coro<void>&, CoroFrameState& state) -> void {
    if (state != CoroFrameState::FinalSuspended) {
        rstd::panic { "async coro result taken before final suspend" };
    }
    state = CoroFrameState::ResultTaken;
}

template<typename T>
class CoroWaitOperation {
public:
    using Output = T;

private:
    coro<T>        m_coro;
    CoroFrameState m_state { CoroFrameState::InitialSuspended };
    Option<T>      m_ready;

public:
    explicit CoroWaitOperation(coro<T>&& task): m_coro(rstd::move(task)) {}

    auto advance(AwaitContext& cx) -> AwaitTransition {
        auto action = cx.execution_domain() == ExecutionDomainKind::ExternalExecutor
                          ? resume_coro_external_segment(m_coro, m_state, cx.poll_context())
                          : resume_coro(m_coro, m_state, cx.poll_context());
        if (action.is_suspended()) {
            return AwaitTransition::suspend();
        }
        if (action.is_submit_completion()) {
            return AwaitTransition::submit_completion(action.facility_id());
        }
        if (action.is_submit_facility()) {
            return AwaitTransition::submit_facility(action.take_endpoint());
        }
        if (action.is_return_to_owner()) {
            return AwaitTransition::return_to_owner();
        }
        if (! action.is_final_suspended()) {
            rstd::panic { "child async coro returned an invalid drive action" };
        }
        m_ready.insert(take_coro_result(m_coro, m_state));
        return AwaitTransition::continue_();
    }

    auto complete_facility(FacilityEvent& event) -> bool {
        return complete_coro_facility(m_coro, m_state, event);
    }

    auto submit_completion(FacilityCompletionToken token) -> FacilityCompletionSubmitResult {
        return submit_coro_completion(m_coro, m_state, rstd::move(token));
    }

    auto take_output() -> T { return rstd::move(m_ready).unwrap_unchecked(); }
};

template<>
class CoroWaitOperation<void> {
public:
    using Output = void;

private:
    coro<void>     m_coro;
    CoroFrameState m_state { CoroFrameState::InitialSuspended };

public:
    explicit CoroWaitOperation(coro<void>&& task): m_coro(rstd::move(task)) {}

    auto advance(AwaitContext& cx) -> AwaitTransition {
        auto action = cx.execution_domain() == ExecutionDomainKind::ExternalExecutor
                          ? resume_coro_external_segment(m_coro, m_state, cx.poll_context())
                          : resume_coro(m_coro, m_state, cx.poll_context());
        if (action.is_suspended()) {
            return AwaitTransition::suspend();
        }
        if (action.is_submit_completion()) {
            return AwaitTransition::submit_completion(action.facility_id());
        }
        if (action.is_submit_facility()) {
            return AwaitTransition::submit_facility(action.take_endpoint());
        }
        if (action.is_return_to_owner()) {
            return AwaitTransition::return_to_owner();
        }
        if (! action.is_final_suspended()) {
            rstd::panic { "child async coro returned an invalid drive action" };
        }
        take_coro_result(m_coro, m_state);
        return AwaitTransition::continue_();
    }

    auto complete_facility(FacilityEvent& event) -> bool {
        return complete_coro_facility(m_coro, m_state, event);
    }

    auto submit_completion(FacilityCompletionToken token) -> FacilityCompletionSubmitResult {
        return submit_coro_completion(m_coro, m_state, rstd::move(token));
    }

    constexpr void take_output() const noexcept {}
};

template<typename T>
struct AwaitableTraits<coro<T>> {
    using Output = T;

    static auto make_suspension(coro<T>&& task) {
        auto operation = CoroWaitOperation<T> { rstd::move(task) };
        return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
    }
};

} // namespace rstd::async

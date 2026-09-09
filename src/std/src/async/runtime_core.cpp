module;
#include <rstd/macro.hpp>

module rstd;
import :async.runtime_core;
import :async.awaitable;
import :async.blocking_pool;
import :async.facility;
import :async.forward;
import :async.poll;
import :env;
import rstd.alloc;
import :sync;
import :thread;

using namespace rstd;
using namespace rstd::literals;

using ::alloc::vec::Vec;
using ::alloc::string::String;
using AsyncPoll = rstd::async::Poll;
using rstd::async::PollApplyStatus;
using rstd::async::PollBatch;
using rstd::async::PollCapabilities;
using rstd::async::PollCommand;
using rstd::async::PollEventData;
using rstd::async::PollEventKind;
using rstd::async::PollEventOwner;
using rstd::async::OperationKey;
using rstd::async::RegistrationKey;
using rstd::async::SourceKey;
using rstd::async::TimerKey;
using rstd::async::IoBackendPreference;
using rstd::async::PollState;
using rstd::async::PollTimeout;
using rstd::async::PollWake;

void TaskStateBase::run_facility_execution(FacilityExecutionToken, TaskAccess) {
    rstd::panic_message("async task cannot resume on external executor",
                        rstd::source_location::current());
}

auto TaskStateBase::submit_completion_facility(FacilityCompletionToken token)
    -> FacilityCompletionSubmitResult {
    return FacilityCompletionSubmitResult::unsupported(rstd::move(token));
}

auto TaskStateBase::submit_facility(FacilityTicket, FacilityRequest) -> FacilitySubmitResult {
    rstd::panic_message("async task cannot submit an execution facility",
                        rstd::source_location::current());
}

auto rstd::async::FacilityJob::operator=(FacilityJob&& other) noexcept -> FacilityJob& {
    if (this != &other) {
        cancel();
        m_token = rstd::move(other.m_token);
    }
    return *this;
}

rstd::async::FacilityJob::~FacilityJob() {
    cancel();
}

void rstd::async::FacilityJob::run() {
    auto task = m_token.access_task();
    if (task.is_some()) {
        auto* state = task->get();
        state->run_facility_execution(rstd::move(m_token), rstd::move(*task));
    }
}

void rstd::async::FacilityJob::cancel() {
    auto task = m_token.access_task();
    if (task.is_some()) {
        (*task)->cancel_facility_handoff(rstd::move(m_token));
    }
}

auto TaskRef::operator=(TaskRef&& other) noexcept -> TaskRef& {
    if (this != &other) {
        reset();
        control = rstd::exchange(other.control, nullptr);
    }
    return *this;
}

TaskRef::~TaskRef() {
    reset();
}

auto TaskRef::clone() const noexcept -> TaskRef {
    if (control != nullptr) {
        control->inc_ref();
    }
    return TaskRef { control };
}

void TaskRef::reset() noexcept {
    if (control != nullptr) {
        auto* current = rstd::exchange(control, nullptr);
        current->dec_ref();
    }
}

void TaskRef::schedule() const {
    auto task = access();
    if (task.is_some()) {
        (*task)->schedule(clone());
    }
}

void TaskRef::abort() const {
    auto task = access();
    if (task.is_some()) {
        (*task)->abort(clone());
    }
}

auto TaskRef::access() const -> Option<TaskAccess> {
    return control == nullptr ? None() : control->acquire();
}

auto TaskAccess::operator=(TaskAccess&& other) noexcept -> TaskAccess& {
    if (this != &other) {
        if (m_control != nullptr) {
            m_control->release_access();
            m_control->dec_ref();
        }
        m_control = rstd::exchange(other.m_control, nullptr);
        m_task    = rstd::exchange(other.m_task, nullptr);
    }
    return *this;
}

TaskAccess::~TaskAccess() {
    if (m_control != nullptr) {
        m_control->release_access();
        m_control->dec_ref();
    }
}

void RuntimeInner::spawn(TaskRef task) {
    auto owner = Option<RuntimeWorkerId> {};
    {
        auto st = m_shared.state.lock().unwrap_unchecked();
        if (st->m_lifecycle == RuntimeLifecycle::Running) {
            st->m_registry.insert(task.clone());
            auto worker = RuntimeWorkerId::current_thread();
            if (is_thread_pool()) {
                auto use_current_worker =
                    has_current_runtime_worker() && CURRENT_RUNTIME == this &&
                    current_execution_domain() == async::ExecutionDomainKind::RuntimeWorker;
                worker = use_current_worker ? current_runtime_worker_id()
                                            : m_shared.next_worker_locked(*st);
            }
            owner = Some(worker);
        }
    }

    if (owner.is_none()) {
        task.abort();
        return;
    }

    auto access = task.access();
    if (access.is_none()) {
        return;
    }
    auto action = (*access)->activate(task.clone(), *owner);
    (*access)->apply(rstd::move(action));
}

auto RuntimeInner::submit_blocking(async::BlockingJob job)
    -> io::Result<async::BlockingJobCancellation> {
    if (! is_running()) {
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
    }
    return blocking_spawner().submit(rstd::move(job));
}

auto RuntimeInner::schedule(ScheduleTicket ticket) -> Result<empty, ScheduleTicket> {
    auto owner = ticket.owner();
    if (CURRENT_RUNTIME == this && CURRENT_RUNTIME_WORKER_CONTEXT != nullptr &&
        current_execution_domain() == async::ExecutionDomainKind::RuntimeWorker &&
        current_runtime_worker_id() == owner) {
        return CURRENT_RUNTIME_WORKER_CONTEXT->enqueue_ready(rstd::move(ticket));
    }
    return m_shared.worker(owner).schedule(rstd::move(ticket));
}

auto RuntimeInner::current_poll_worker() -> io::Result<WorkerHandle> {
    if (CURRENT_RUNTIME != this || ! has_current_runtime_worker() ||
        current_execution_domain() != async::ExecutionDomainKind::RuntimeWorker) {
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
    }
    return Ok(m_shared.worker_handle(current_runtime_worker_id()));
}

auto RuntimeInner::reserve_current_operation(PollEventOwner owner) -> io::Result<OperationKey> {
    if (CURRENT_RUNTIME != this || CURRENT_RUNTIME_WORKER_CONTEXT == nullptr ||
        current_execution_domain() != async::ExecutionDomainKind::RuntimeWorker) {
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
    }
    return CURRENT_RUNTIME_WORKER_CONTEXT->reserve_operation(rstd::move(owner));
}

void RuntimeInner::abandon_current_operation(OperationKey key) {
    if (CURRENT_RUNTIME != this || CURRENT_RUNTIME_WORKER_CONTEXT == nullptr) {
        rstd::panic { "operation abandoned outside its runtime worker" };
    }
    CURRENT_RUNTIME_WORKER_CONTEXT->abandon_operation(key);
}

auto RuntimeInner::defer_current_poll(PollCommand command) -> Result<empty, PollCommand> {
    if (CURRENT_RUNTIME != this || CURRENT_RUNTIME_WORKER_CONTEXT == nullptr ||
        current_execution_domain() != async::ExecutionDomainKind::RuntimeWorker) {
        return Err(rstd::move(command));
    }
    return CURRENT_RUNTIME_WORKER_CONTEXT->defer_poll(rstd::move(command));
}

auto RuntimeInner::lifecycle() -> RuntimeLifecycle {
    auto st = m_shared.state.lock().unwrap_unchecked();
    return st->m_lifecycle;
}

auto RuntimeInner::is_running() -> bool {
    return lifecycle() == RuntimeLifecycle::Running;
}

auto RuntimeInner::is_stopping() -> bool {
    auto state = lifecycle();
    return state == RuntimeLifecycle::Stopping || state == RuntimeLifecycle::Stopped;
}

void RuntimeInner::retire(TaskRefControl* task) {
    {
        auto st = m_shared.state.lock().unwrap_unchecked();
        st->m_registry.remove(task);
    }
    m_shared.task_cvar.notify_all();
}

auto RuntimeInner::complete_facility(FacilityEvent event) -> Result<empty, FacilityEvent> {
    auto owner = event.token().owner_worker;
    if (CURRENT_RUNTIME == this && CURRENT_RUNTIME_WORKER_CONTEXT != nullptr &&
        current_execution_domain() == async::ExecutionDomainKind::RuntimeWorker &&
        current_runtime_worker_id() == owner) {
        return CURRENT_RUNTIME_WORKER_CONTEXT->complete_facility(rstd::move(event));
    }
    return m_shared.worker(owner).complete_facility(rstd::move(event));
}

auto FacilityCompletionToken::complete(FacilityEventKind kind) -> bool {
    auto task = m_task.access();
    if (task.is_none()) {
        return false;
    }
    auto rt = (*task)->runtime.upgrade();
    if (! rt) {
        m_task.abort();
        return false;
    }

    auto submitted = rt->complete_facility(into_event(kind));
    if (submitted.is_ok()) {
        return true;
    }
    auto rejected = rstd::move(submitted).unwrap_err_unchecked();
    rejected.take_task().abort();
    return false;
}

auto FacilityCompletionToken::complete_poll(PollEventData event) -> bool {
    auto task = m_task.access();
    if (task.is_none()) {
        return false;
    }
    auto rt = (*task)->runtime.upgrade();
    if (! rt) {
        m_task.abort();
        return false;
    }

    auto submitted = rt->complete_facility(into_poll_event(rstd::move(event)));
    if (submitted.is_ok()) {
        return true;
    }
    auto rejected = rstd::move(submitted).unwrap_err_unchecked();
    rejected.take_task().abort();
    return false;
}

auto RuntimeInner::complete_facility_batch(FacilityEventBatch batch)
    -> Result<empty, FacilityEventBatch> {
    auto owner = batch.owner_worker();
    return m_shared.worker(owner).complete_facility_batch(rstd::move(batch));
}

void TaskStateBase::complete_locked(TaskControl& state) {
    state.lifecycle = TaskLifecycle::Completed;
    ++state.schedule_generation;
    ++state.facility_generation;
    state.wake_requested = false;
}

auto TaskStateBase::make_schedule_action(TaskControl& state, TaskRef task) -> TaskAction {
    ++state.schedule_generation;
    state.lifecycle = TaskLifecycle::Queued;
    return TaskAction::schedule(
        ScheduleTicket { rstd::move(task), state.owner_worker, state.schedule_generation });
}

auto TaskStateBase::activate(TaskRef self, RuntimeWorkerId owner) -> TaskAction {
    auto state = control.lock().unwrap_unchecked();
    if (state->lifecycle != TaskLifecycle::Created || state->cancel_requested) {
        return TaskAction::none();
    }
    state->owner_worker = owner;
    return make_schedule_action(*state, rstd::move(self));
}

auto TaskStateBase::try_begin_runtime(ScheduleTicket ticket, TaskAccess access)
    -> Option<RuntimeExecutionLease> {
    auto state = control.lock().unwrap_unchecked();
    if (state->lifecycle != TaskLifecycle::Queued || state->cancel_requested ||
        state->owner_worker != ticket.owner() ||
        state->schedule_generation != ticket.generation()) {
        return None();
    }
    state->lifecycle = TaskLifecycle::RunningRuntime;
    return Some(
        RuntimeExecutionLease { ticket.take_task(), rstd::move(access), ticket.generation() });
}

void TaskStateBase::apply(TaskAction action) {
    switch (action.kind()) {
    case TaskActionKind::None: return;
    case TaskActionKind::Schedule: {
        auto ticket = action.take_ticket();
        auto rt     = runtime.upgrade();
        if (! rt) {
            auto task = ticket.take_task();
            task.abort();
            return;
        }
        auto submitted = rt->schedule(rstd::move(ticket));
        if (submitted.is_err()) {
            auto rejected = rstd::move(submitted).unwrap_err_unchecked();
            auto task     = rejected.take_task();
            task.abort();
        }
        return;
    }
    case TaskActionKind::SubmitCompletion: {
        auto token = action.take_completion_token();
        auto task  = token.access_task();
        if (task.is_none()) {
            return;
        }
        auto submitted = (*task)->submit_completion_facility(rstd::move(token));
        if (submitted.status() == FacilitySubmitResult::Accepted) {
            (*task)->install_completion_cancellation(submitted.take_cancellation());
        } else {
            (void)submitted.take_token().complete(FacilityEventKind::Error);
        }
        return;
    }
    case TaskActionKind::SubmitFacility: {
        auto ticket  = action.take_facility_ticket();
        auto request = action.take_facility_request();
        auto task    = ticket.access_task();
        if (task.is_some()) {
            (void)(*task)->submit_facility(rstd::move(ticket), rstd::move(request));
        }
        return;
    }
    case TaskActionKind::CompleteValue: {
        auto task   = action.take_terminal_task();
        auto access = task.access();
        if (access.is_none()) {
            return;
        }
        (*access)->complete_value();
        if (auto rt = (*access)->runtime.upgrade()) {
            rt->retire(task.identity());
        }
        return;
    }
    case TaskActionKind::CompleteAbort: {
        auto cancellation = action.take_cancellation();
        if (cancellation.is_some()) {
            rstd::move(cancellation).unwrap_unchecked().cancel();
        }
        auto task   = action.take_terminal_task();
        auto access = task.access();
        if (access.is_none()) {
            return;
        }
        (*access)->complete_abort();
        if (auto rt = (*access)->runtime.upgrade()) {
            rt->retire(task.identity());
        }
        return;
    }
    }
}

void TaskStateBase::schedule(TaskRef self) {
    auto rt       = runtime.upgrade();
    bool stopping = ! rt || rt->is_stopping();
    auto action   = TaskAction::none();
    {
        auto state = control.lock().unwrap_unchecked();
        switch (state->lifecycle) {
        case TaskLifecycle::Completed:
        case TaskLifecycle::Queued: return;
        case TaskLifecycle::Created:
            if (stopping) {
                state->cancel_requested = true;
                complete_locked(*state);
                action =
                    TaskAction::complete_abort(rstd::move(self), take_completion_cancellation());
            } else {
                state->wake_requested = true;
            }
            break;
        case TaskLifecycle::RunningRuntime:
        case TaskLifecycle::FacilityQueued:
        case TaskLifecycle::FacilityRunning:
            if (stopping) {
                state->cancel_requested = true;
            } else {
                state->wake_requested = true;
            }
            break;
        case TaskLifecycle::Waiting:
            if (stopping) {
                state->cancel_requested = true;
                complete_locked(*state);
                action =
                    TaskAction::complete_abort(rstd::move(self), take_completion_cancellation());
            } else {
                action = make_schedule_action(*state, rstd::move(self));
            }
            break;
        }
    }
    apply(rstd::move(action));
}

void TaskStateBase::abort(TaskRef self) {
    auto action = TaskAction::none();
    {
        auto state = control.lock().unwrap_unchecked();
        if (state->lifecycle == TaskLifecycle::Completed) {
            return;
        }

        state->cancel_requested = true;
        if (state->lifecycle == TaskLifecycle::Created ||
            state->lifecycle == TaskLifecycle::Queued ||
            state->lifecycle == TaskLifecycle::Waiting ||
            state->lifecycle == TaskLifecycle::FacilityQueued) {
            complete_locked(*state);
            action = TaskAction::complete_abort(rstd::move(self), take_completion_cancellation());
        }
    }

    apply(rstd::move(action));
}

void TaskStateBase::install_completion_cancellation(FacilityCancellation cancellation) {
    auto identity  = cancellation.token();
    bool installed = false;
    {
        auto state = control.lock().unwrap_unchecked();
        if (identity.effect == FacilityEffect::CompletionOnly &&
            state->lifecycle == TaskLifecycle::Waiting &&
            state->owner_worker == identity.owner_worker &&
            state->facility_id == identity.facility_id &&
            state->facility_generation == identity.generation) {
            auto current = completion_cancellation.lock().unwrap_unchecked();
            if (current->is_none()) {
                *current  = Some(rstd::move(cancellation));
                installed = true;
            }
        }
    }
    if (! installed) {
        cancellation.cancel();
    }
}

auto TaskStateBase::take_completion_cancellation() -> Option<FacilityCancellation> {
    auto cancellation = completion_cancellation.lock().unwrap_unchecked();
    return cancellation->take();
}

auto TaskStateBase::take_completion_event() -> Option<FacilityEvent> {
    auto event = completion_event.lock().unwrap_unchecked();
    return event->take();
}

auto TaskStateBase::end_runtime_execution(RuntimeExecutionLease lease, TaskPollAction outcome)
    -> TaskAction {
    auto rt         = runtime.upgrade();
    bool stopping   = ! rt || rt->is_stopping();
    auto action     = TaskAction::none();
    auto generation = lease.generation();
    auto self       = lease.take_task();
    auto request =
        outcome.is_submit_facility() ? Some(outcome.take_request()) : Option<FacilityRequest> {};
    auto completion_id =
        outcome.is_submit_completion() ? Some(outcome.completion_id()) : Option<FacilityId> {};
    {
        auto state = control.lock().unwrap_unchecked();
        if (state->schedule_generation != generation ||
            state->lifecycle != TaskLifecycle::RunningRuntime) {
            return TaskAction::none();
        }

        if (state->cancel_requested || stopping) {
            state->cancel_requested = true;
            complete_locked(*state);
            action = TaskAction::complete_abort(rstd::move(self), take_completion_cancellation());
        } else if (outcome.is_complete()) {
            complete_locked(*state);
            action = TaskAction::complete_value(rstd::move(self));
        } else if (completion_id.is_some()) {
            auto id = *completion_id;
            ++state->facility_generation;
            state->facility_id    = id;
            state->lifecycle      = TaskLifecycle::Waiting;
            state->wake_requested = false;
            action                = TaskAction::submit_completion(FacilityCompletionToken {
                rstd::move(self),
                FacilityToken { id,
                                state->owner_worker,
                                state->facility_generation,
                                FacilityEffect::CompletionOnly },
            });
        } else if (request.is_some()) {
            auto facility_request = rstd::move(request).unwrap_unchecked();
            if (facility_request.effect() != FacilityEffect::ExecuteTaskSegment) {
                rstd::panic { "runtime task submitted an unsupported completion facility" };
            }
            auto id = facility_request.id();
            ++state->facility_generation;
            state->facility_id    = id;
            state->lifecycle      = TaskLifecycle::FacilityQueued;
            state->wake_requested = false;
            action                = TaskAction::submit_facility(
                FacilityTicket {
                    rstd::move(self),
                    FacilityToken { id,
                                    state->owner_worker,
                                    state->facility_generation,
                                    FacilityEffect::ExecuteTaskSegment },
                },
                rstd::move(facility_request));
        } else if (state->wake_requested) {
            state->wake_requested = false;
            action                = make_schedule_action(*state, rstd::move(self));
        } else {
            state->lifecycle = TaskLifecycle::Waiting;
        }
    }
    return action;
}

void TaskStateBase::complete_facility(FacilityEvent event) {
    auto rt           = runtime.upgrade();
    bool stopping     = ! rt || rt->is_stopping();
    auto metadata     = event.token();
    auto self         = event.take_task();
    auto action       = TaskAction::none();
    auto cancellation = Option<FacilityCancellation> {};
    bool deliver      = false;
    {
        auto state = control.lock().unwrap_unchecked();
        if (metadata.effect != FacilityEffect::CompletionOnly ||
            state->lifecycle != TaskLifecycle::Waiting ||
            state->owner_worker != metadata.owner_worker ||
            state->facility_id != metadata.facility_id ||
            state->facility_generation != metadata.generation) {
            return;
        }
        if (state->cancel_requested || stopping) {
            state->cancel_requested = true;
            complete_locked(*state);
            action = TaskAction::complete_abort(rstd::move(self), take_completion_cancellation());
        } else {
            cancellation = take_completion_cancellation();
            action       = make_schedule_action(*state, rstd::move(self));
            deliver      = true;
        }
    }
    if (cancellation.is_some()) {
        rstd::move(cancellation).unwrap_unchecked().disarm();
    }
    if (deliver) {
        auto pending = completion_event.lock().unwrap_unchecked();
        if (pending->is_some()) {
            rstd::panic { "async task received overlapping facility events" };
        }
        *pending = Some(rstd::move(event));
    }
    apply(rstd::move(action));
}

auto TaskStateBase::begin_facility_execution(FacilityExecutionToken token, TaskAccess access)
    -> Option<FacilityExecutionLease> {
    auto rt       = runtime.upgrade();
    bool stopping = ! rt || rt->is_stopping();
    auto metadata = token.token();
    auto self     = token.take_task();
    auto lease    = Option<FacilityExecutionLease> {};
    auto action   = TaskAction::none();
    {
        auto state = control.lock().unwrap_unchecked();
        if (state->lifecycle != TaskLifecycle::FacilityQueued ||
            state->owner_worker != metadata.owner_worker ||
            state->facility_id != metadata.facility_id ||
            state->facility_generation != metadata.generation ||
            metadata.effect != FacilityEffect::ExecuteTaskSegment) {
            return None();
        }

        if (state->cancel_requested || stopping) {
            state->cancel_requested = true;
            complete_locked(*state);
            action = TaskAction::complete_abort(rstd::move(self), take_completion_cancellation());
        } else {
            state->lifecycle = TaskLifecycle::FacilityRunning;
            lease = Some(FacilityExecutionLease { rstd::move(self), rstd::move(access), metadata });
        }
    }

    apply(rstd::move(action));
    return lease;
}

void TaskStateBase::cancel_facility_handoff(FacilityExecutionToken token) {
    auto rt       = runtime.upgrade();
    bool stopping = ! rt || rt->is_stopping();
    auto metadata = token.token();
    auto self     = token.take_task();
    auto action   = TaskAction::none();
    {
        auto state = control.lock().unwrap_unchecked();
        if (state->lifecycle != TaskLifecycle::FacilityQueued ||
            state->owner_worker != metadata.owner_worker ||
            state->facility_id != metadata.facility_id ||
            state->facility_generation != metadata.generation) {
            return;
        }

        if (state->cancel_requested || stopping) {
            state->cancel_requested = true;
            complete_locked(*state);
            action = TaskAction::complete_abort(rstd::move(self), take_completion_cancellation());
        } else {
            state->wake_requested = false;
            action                = make_schedule_action(*state, rstd::move(self));
        }
    }
    apply(rstd::move(action));
}

auto TaskStateBase::end_facility_execution(FacilityExecutionLease lease, TaskPollAction outcome)
    -> TaskAction {
    auto rt       = runtime.upgrade();
    bool stopping = ! rt || rt->is_stopping();
    auto metadata = lease.token();
    auto self     = lease.take_task();
    auto action   = TaskAction::none();
    auto request =
        outcome.is_submit_facility() ? Some(outcome.take_request()) : Option<FacilityRequest> {};
    {
        auto state = control.lock().unwrap_unchecked();
        if (state->lifecycle != TaskLifecycle::FacilityRunning ||
            state->owner_worker != metadata.owner_worker ||
            state->facility_id != metadata.facility_id ||
            state->facility_generation != metadata.generation) {
            return TaskAction::none();
        }

        if (state->cancel_requested || stopping) {
            state->cancel_requested = true;
            complete_locked(*state);
            action = TaskAction::complete_abort(rstd::move(self), take_completion_cancellation());
        } else if (outcome.is_complete()) {
            rstd::panic { "external facility cannot complete a runtime task directly" };
        } else if (outcome.is_submit_completion()) {
            rstd::panic { "external facility cannot submit a runtime-driven completion" };
        } else if (request.is_some()) {
            auto facility_request = rstd::move(request).unwrap_unchecked();
            if (facility_request.effect() != FacilityEffect::ExecuteTaskSegment) {
                rstd::panic { "external segment submitted an unsupported completion facility" };
            }
            auto id = facility_request.id();
            ++state->facility_generation;
            state->facility_id    = id;
            state->lifecycle      = TaskLifecycle::FacilityQueued;
            state->wake_requested = false;
            action                = TaskAction::submit_facility(
                FacilityTicket {
                    rstd::move(self),
                    FacilityToken { id,
                                    state->owner_worker,
                                    state->facility_generation,
                                    FacilityEffect::ExecuteTaskSegment },
                },
                rstd::move(facility_request));
        } else {
            state->wake_requested = false;
            action                = make_schedule_action(*state, rstd::move(self));
        }
    }
    return action;
}

auto task_waker_clone(voidp data) -> task::RawWaker;
void task_waker_wake(voidp data);
void task_waker_wake_by_ref(voidp data);
void task_waker_drop(voidp data);

const task::RawWakerVTable TASK_WAKER_VTABLE {
    &task_waker_clone,
    &task_waker_wake,
    &task_waker_wake_by_ref,
    &task_waker_drop,
};

auto task_waker_clone(voidp data) -> task::RawWaker {
    auto* control = static_cast<TaskRefControl*>(data);
    control->inc_ref();
    return task::RawWaker::from_raw_parts(control, rstd::addressof(TASK_WAKER_VTABLE));
}

void task_waker_wake(voidp data) {
    TaskRef::adopt(static_cast<TaskRefControl*>(data)).schedule();
}

void task_waker_wake_by_ref(voidp data) {
    auto* control = static_cast<TaskRefControl*>(data);
    control->inc_ref();
    TaskRef::adopt(control).schedule();
}

void task_waker_drop(voidp data) {
    static_cast<TaskRefControl*>(data)->dec_ref();
}

auto make_task_waker(const TaskRef& task) -> task::Waker {
    auto* control = task.identity();
    control->inc_ref();
    return task::Waker::from_raw(
        task::RawWaker::from_raw_parts(control, rstd::addressof(TASK_WAKER_VTABLE)));
}

void poll_runtime_task(RuntimeExecutionLease lease) {
    auto& task_ref   = lease.task();
    auto* task_state = rstd::addressof(lease.state());
    auto  waker      = make_task_waker(task_ref);
    auto  cx         = task::Context { waker };
    auto  outcome    = task_state->poll(task_ref, cx);
    auto  action     = task_state->end_runtime_execution(rstd::move(lease), rstd::move(outcome));
    task_state->apply(rstd::move(action));
}

RuntimeWorker::RuntimeWorker(RuntimeInner& runtime, WorkerHandle handle)
    : m_runtime(rstd::addressof(runtime)),
      m_handle(rstd::move(handle)),
      m_ready(),
      m_local_poll_commands(Vec<PollCommand>::make()),
      m_poll_state(None()),
      m_poll_init_error(None()) {
    auto initialized = rstd::async::PollRuntimeAccess::init(runtime.io_backend_preference(),
                                                            runtime.blocking_spawner());
    if (initialized.is_err()) {
        m_poll_init_error = Some(rstd::move(initialized).unwrap_err_unchecked());
        return;
    }

    auto poll         = rstd::move(initialized).unwrap_unchecked();
    auto capabilities = AsyncPoll::capabilities(poll.state);
    if (! m_handle.install_poll(rstd::move(poll.wake), capabilities)) {
        (void)AsyncPoll::shutdown(poll.state);
        m_poll_init_error =
            Some(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
        return;
    }
    m_poll_state = Some(rstd::move(poll.state));
}

auto RuntimeWorker::reserve_operation(PollEventOwner owner) -> io::Result<OperationKey> {
    if (m_poll_state.is_none() || m_stop_requested) {
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
    }
    if (AsyncPoll::has_recycled_operation(*m_poll_state)) {
        return Ok(AsyncPoll::reserve_recycled_operation(*m_poll_state, rstd::move(owner)));
    }
    auto key = m_handle.allocate_operation_key();
    if (! AsyncPoll::reserve_fresh_operation(*m_poll_state, key, rstd::move(owner))) {
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
    }
    return Ok(key);
}

auto RuntimeWorker::defer_poll(PollCommand command) -> Result<empty, PollCommand> {
    if (m_poll_state.is_none() || m_stop_requested) return Err(rstd::move(command));
    m_local_poll_commands.push(rstd::move(command));
    return Ok(empty {});
}

auto RuntimeWorker::enqueue_ready(ScheduleTicket ticket) -> Result<empty, ScheduleTicket> {
    if (m_stop_requested) return Err(rstd::move(ticket));
    m_ready.push(rstd::move(ticket));
    return Ok(empty {});
}

auto RuntimeWorker::complete_facility(FacilityEvent event) -> Result<empty, FacilityEvent> {
    if (m_stop_requested) return Err(rstd::move(event));
    auto task = event.access_task();
    if (task.is_some()) {
        (*task)->complete_facility(rstd::move(event));
    }
    return Ok(empty {});
}

void RuntimeWorker::abandon_operation(OperationKey key) {
    if (m_poll_state.is_none()) return;
    (void)AsyncPoll::abandon_operation(*m_poll_state, key);
}

RuntimeWorker::~RuntimeWorker() {
    drain_local_poll();
    m_handle.clear_poll();
    if (m_poll_state.is_some()) {
        dispatch_poll_batch(AsyncPoll::shutdown(*m_poll_state));
    }
}

void RuntimeWorker::drain_inbox() {
    while (true) {
        auto command = m_handle.pop_command();
        if (command.is_none()) {
            return;
        }

        auto value = rstd::move(command).unwrap_unchecked();
        switch (value.kind()) {
        case WorkerCommandKind::Schedule: m_ready.push(value.take_ticket()); break;
        case WorkerCommandKind::FacilityComplete: {
            auto event = value.take_event();
            auto task  = event.access_task();
            if (task.is_some()) {
                (*task)->complete_facility(rstd::move(event));
            }
            break;
        }
        case WorkerCommandKind::FacilityBatch: {
            auto batch = value.take_batch();
            while (! batch.is_empty()) {
                auto event = rstd::move(batch.pop_front()).unwrap_unchecked();
                auto task  = event.access_task();
                if (task.is_some()) {
                    (*task)->complete_facility(rstd::move(event));
                }
            }
            break;
        }
        case WorkerCommandKind::Poll: apply_poll(value.take_poll()); break;
        case WorkerCommandKind::Stop:
            m_stop_requested = true;
            m_handle.begin_draining();
            return;
        }
    }
}

void RuntimeWorker::drain_local_poll() {
    for (rstd::size_t i = 0; i < m_local_poll_commands.len().to_primitive(); ++i) {
        apply_poll(rstd::move(m_local_poll_commands[usize(i)]));
    }
    m_local_poll_commands.clear();
}

void RuntimeWorker::apply_poll(PollCommand command) {
    if (m_poll_state.is_none()) {
        if (! command.can_dispatch_error()) return;
        auto event = rstd::move(command).into_error_event(
            io::Error::from_kind(io::ErrorKind { io::ErrorKind::NotConnected }));
        event.dispatch();
        return;
    }

    auto applied = AsyncPoll::apply(*m_poll_state, rstd::move(command));
    if (applied.status() == PollApplyStatus::Accepted) {
        if (applied.has_event()) {
            auto event = applied.take_event();
            event.dispatch();
        }
        return;
    }

    auto rejected = applied.take_command();
    auto error    = applied.take_error();
    if (! rejected.can_dispatch_error()) return;
    auto event = rstd::move(rejected).into_error_event(rstd::move(error));
    event.dispatch();
}

void RuntimeWorker::drain_ready() {
    drain_inbox();
    if (m_stop_requested) {
        m_ready.clear();
        return;
    }
    auto remaining = m_cooperative_budget;
    while (remaining > usize()) {
        auto next = m_ready.pop_front();
        if (next.is_none()) {
            return;
        }
        --remaining;

        auto ticket = rstd::move(next).unwrap_unchecked();
        auto task   = ticket.access_task();
        if (task.is_none()) {
            continue;
        }
        auto* task_state = task->get();
        auto  lease      = task_state->try_begin_runtime(rstd::move(ticket), rstd::move(*task));
        if (lease.is_none()) {
            continue;
        }

        poll_runtime_task(rstd::move(lease).unwrap_unchecked());
        drain_local_poll();
        drain_inbox();
    }
}

void RuntimeWorker::wait_for_work() {
    poll_backend(m_ready.is_empty() ? PollTimeout::Infinite : PollTimeout::Immediate);
}

void RuntimeWorker::poll_backend(PollTimeout timeout) {
    if (m_poll_state.is_none()) {
        rstd::panic { "async runtime worker Poll initialization failed" };
    }

    auto polled = AsyncPoll::poll(*m_poll_state, timeout);
    if (polled.is_err()) {
        rstd::panic { "async runtime worker Poll failed" };
    }

    dispatch_poll_batch(rstd::move(polled).unwrap_unchecked());
}

void RuntimeWorker::dispatch_poll_batch(PollBatch batch) {
    while (! batch.is_empty()) {
        auto event = rstd::move(batch.pop_front()).unwrap_unchecked();
        if (event.kind() != rstd::async::PollEventKind::Wake) {
            event.dispatch();
        }
    }
}

void RuntimeInner::worker_loop(RuntimeWorkerId worker) {
    RuntimeWorker { *this, m_shared.worker_handle(worker) }.run();
}

void RuntimeWorker::run() {
    auto scope        = RuntimeScope { *m_runtime };
    auto worker_scope = RuntimeWorkerScope { m_handle.id(), this };

    if (! m_handle.begin_start()) {
        m_handle.finish_stop();
        return;
    }
    if (m_poll_init_error.is_some()) {
        auto error = rstd::move(m_poll_init_error).unwrap_unchecked();
        m_handle.finish_stop();
        m_runtime->worker_start_failed(rstd::move(error));
        return;
    }
    if (! m_handle.finish_start()) {
        m_handle.finish_stop();
        return;
    }
    m_runtime->worker_started();

    for (;;) {
        drain_ready();
        if (m_stop_requested) {
            m_handle.finish_stop();
            return;
        }
        poll_backend(m_ready.is_empty() ? PollTimeout::Infinite : PollTimeout::Immediate);
    }
}

void RuntimeInner::worker_started() {
    {
        auto state = m_shared.state.lock().unwrap_unchecked();
        ++state->m_running_workers;
    }
    m_shared.worker_cvar.notify_all();
}

void RuntimeInner::worker_start_failed(io::Error error) {
    {
        auto state = m_shared.state.lock().unwrap_unchecked();
        if (state->m_worker_start_error.is_none()) {
            state->m_worker_start_error = Some(rstd::move(error));
        }
    }
    m_shared.worker_cvar.notify_all();
}

auto RuntimeInner::wait_for_startup() -> io::Result<empty> {
    auto state = m_shared.state.lock().unwrap_unchecked();
    m_shared.worker_cvar.wait_while(state, [this](RuntimeSharedState const& shared) {
        return shared.m_lifecycle == RuntimeLifecycle::Building &&
               shared.m_worker_start_error.is_none() &&
               shared.m_running_workers < m_shared.worker_count();
    });
    if (state->m_worker_start_error.is_some()) {
        return Err(rstd::move(state->m_worker_start_error).unwrap_unchecked());
    }
    if (state->m_lifecycle != RuntimeLifecycle::Building ||
        state->m_running_workers != m_shared.worker_count()) {
        rstd::panic { "async runtime worker startup interrupted" };
    }
    state->m_lifecycle = RuntimeLifecycle::Running;
    return Ok(empty {});
}

auto RuntimeInner::begin_shutdown() -> bool {
    auto state = m_shared.state.lock().unwrap_unchecked();
    if (state->m_lifecycle == RuntimeLifecycle::Stopping ||
        state->m_lifecycle == RuntimeLifecycle::Stopped) {
        return false;
    }
    state->m_lifecycle = RuntimeLifecycle::Stopping;
    m_shared.worker_cvar.notify_all();
    return true;
}

void RuntimeInner::stop_workers() {
    m_shared.request_worker_stop();
    if (! is_thread_pool()) {
        m_shared.worker(RuntimeWorkerId::current_thread()).clear_inbox();
        m_shared.worker(RuntimeWorkerId::current_thread()).finish_stop();
    }
}

void RuntimeInner::finish_shutdown() {
    {
        auto state         = m_shared.state.lock().unwrap_unchecked();
        state->m_lifecycle = RuntimeLifecycle::Stopped;
    }
    m_shared.task_cvar.notify_all();
    m_shared.worker_cvar.notify_all();
}

void RuntimeInner::abort_all_tasks() {
    auto tasks = Vec<TaskRef>::make();
    {
        auto st = m_shared.state.lock().unwrap_unchecked();
        tasks   = st->m_registry.clone_all();
    }

    while (! tasks.is_empty()) {
        auto task = rstd::move(tasks.pop()).unwrap_unchecked();
        task.abort();
    }

    {
        auto st = m_shared.state.lock().unwrap_unchecked();
        m_shared.task_cvar.wait_while(st, [](RuntimeSharedState const& runtime_state) {
            return ! runtime_state.m_registry.is_empty();
        });
    }
    m_shared.clear_inbox();
}

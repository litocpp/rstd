export module rstd:async.spawn;
export import :async.awaitable;
export import :async.runtime_core;
import :async.facility;
import :async.runtime_driver;
import :async.task;
import :async.terminal;
import rstd.alloc;
import :sync;

using namespace rstd;
using rstd::async::JoinError;

struct JoinHandleFactory;

template<typename T>
struct JoinState {
    using Stored = mtp::void_empty_t<T>;
    using Output = Result<Stored, JoinError>;

    struct Fields {
        TerminalCell<Output> terminal;
        Option<task::Waker>  waker;
        Option<TaskRef>      task;
    };

    sync::Mutex<Fields> fields;
    sync::Condvar       ready_cvar;

    JoinState(): fields(Fields {}), ready_cvar(sync::Condvar::make()) {}

    void set_task(TaskRef task) {
        auto f  = fields.lock().unwrap_unchecked();
        f->task = Some(rstd::move(task));
    }

    auto complete_value(Stored in) -> Option<task::Waker> {
        auto waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (! f->terminal.publish(Output { Ok(rstd::move(in)) })) {
                return None();
            }
            f->task = None();
            waker   = f->waker.take();
        }
        ready_cvar.notify_all();
        return waker;
    }

    auto complete_abort() -> Option<task::Waker> {
        auto waker = Option<task::Waker> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (! f->terminal.publish(Output { Err(JoinError {}) })) {
                return None();
            }
            f->task = None();
            waker   = f->waker.take();
        }
        ready_cvar.notify_all();
        return waker;
    }

    auto poll(task::Context& cx) -> task::Poll<Output> {
        auto f = fields.lock().unwrap_unchecked();
        if (f->terminal.is_ready()) {
            return task::Poll<Output>::Ready(f->terminal.take());
        }
        if (f->terminal.is_consumed()) {
            rstd::panic { "JoinHandle polled after completion" };
        }
        if (f->terminal.is_closed()) {
            rstd::panic { "JoinHandle polled after close" };
        }
        f->waker = Some(cx.waker().clone());
        return task::Poll<Output>::Pending();
    }

    auto is_ready() const -> bool {
        auto f = fields.lock().unwrap_unchecked();
        return f->terminal.is_terminal();
    }

    auto wait() -> Output {
        auto f = fields.lock().unwrap_unchecked();
        ready_cvar.wait_while(f, [](Fields const& state) {
            return state.terminal.is_pending();
        });
        if (f->terminal.is_consumed()) {
            rstd::panic { "JoinHandle waited after completion" };
        }
        if (f->terminal.is_closed()) {
            rstd::panic { "JoinHandle waited after close" };
        }
        return f->terminal.take();
    }

    void abort_task() {
        auto task = Option<TaskRef> {};
        {
            auto f = fields.lock().unwrap_unchecked();
            if (f->task.is_some()) {
                task = Some(f->task->clone());
            }
        }
        if (task.is_some()) {
            task->abort();
        }
    }
};

template<typename T>
class JoinStateOwner {
    Option<sync::Arc<JoinState<T>>> m_owned;
    JoinState<T>*                   m_state { nullptr };

    JoinStateOwner(Option<sync::Arc<JoinState<T>>> owned, JoinState<T>* state)
        : m_owned(rstd::move(owned)), m_state(state) {}

public:
    JoinStateOwner(const JoinStateOwner&)                        = delete;
    auto operator=(const JoinStateOwner&) -> JoinStateOwner&     = delete;
    JoinStateOwner(JoinStateOwner&&) noexcept                    = default;
    auto operator=(JoinStateOwner&&) noexcept -> JoinStateOwner& = default;

    static auto owned(sync::Arc<JoinState<T>> state) -> JoinStateOwner {
        auto* ptr = state.as_ptr().as_raw_ptr();
        return JoinStateOwner { Some(rstd::move(state)), ptr };
    }

    static auto borrowed(JoinState<T>& state) -> JoinStateOwner {
        return JoinStateOwner { None(), rstd::addressof(state) };
    }

    auto operator->() const noexcept -> JoinState<T>* { return m_state; }
};

template<typename T>
auto spawn_driver_on(RuntimeInner& runtime, rstd::async::RuntimeCoroDriver<T> driver)
    -> rstd::async::JoinHandle<T>;

namespace rstd::async
{

export template<typename T>
class JoinHandle {
    sync::Arc<JoinState<T>> state;

    explicit JoinHandle(sync::Arc<JoinState<T>> state): state(rstd::move(state)) {}

    friend struct ::JoinHandleFactory;
    friend class Runtime;

    auto blocking_wait() -> Result<mtp::void_empty_t<T>, JoinError> { return state->wait(); }

public:
    using Output = Result<mtp::void_empty_t<T>, JoinError>;

    JoinHandle(const JoinHandle&)                        = delete;
    JoinHandle& operator=(const JoinHandle&)             = delete;
    JoinHandle(JoinHandle&&) noexcept                    = default;
    auto operator=(JoinHandle&&) noexcept -> JoinHandle& = default;
    ~JoinHandle()                                        = default;

    void abort() { state->abort_task(); }

    auto is_finished() const -> bool { return state->is_ready(); }

    auto poll(mut_ref<JoinHandle> self, task::Context& cx) -> task::Poll<Output> {
        return self->state->poll(cx);
    }
};

} // namespace rstd::async

struct JoinHandleFactory {
    template<typename T>
    static auto make(sync::Arc<JoinState<T>> state) -> rstd::async::JoinHandle<T> {
        return rstd::async::JoinHandle<T> { rstd::move(state) };
    }
};

template<typename T>
struct DriverTaskState : TaskStateBase {
    using Stored = mtp::void_empty_t<T>;

    rstd::async::RuntimeCoroDriver<T> driver;
    JoinStateOwner<T>                 join;
    Option<Stored>                    ready;

    DriverTaskState(TaskRefControl&                   ref_control,
                    sync::Weak<RuntimeInner>          runtime,
                    rstd::async::RuntimeCoroDriver<T> driver,
                    JoinStateOwner<T>                 join)
        : TaskStateBase(ref_control, rstd::move(runtime)),
          driver(rstd::move(driver)),
          join(rstd::move(join)) {}

    auto poll(TaskRef&, task::Context& cx) -> TaskPollAction override {
        if (auto event = take_completion_event(); event.is_some()) {
            auto completion = rstd::move(event).unwrap_unchecked();
            if (! driver.complete_facility(completion)) {
                rstd::panic { "async facility event has no matching await operation" };
            }
        }
        auto out = driver.drive(cx);
        if (out.is_suspended()) {
            return TaskPollAction::none();
        }
        if (out.is_submit_completion()) {
            return TaskPollAction::submit_completion(FacilityId { out.facility_id() });
        }
        if (out.is_submit_facility()) {
            return TaskPollAction::submit_facility(out.take_endpoint());
        }
        if (out.is_return_to_owner()) {
            rstd::panic { "runtime coroutine returned to its current owner" };
        }
        if (! out.is_final_suspended()) {
            rstd::panic { "runtime coroutine driver returned an invalid action" };
        }

        if constexpr (mtp::is_void<T>) {
            driver.take_result();
            ready = Some(empty {});
        } else {
            ready = Some(driver.take_result());
        }
        return TaskPollAction::complete();
    }

    void complete_value() override {
        if (ready.is_none()) {
            rstd::panic { "async coroutine task completed without a value" };
        }
        auto join_waker = join->complete_value(rstd::move(ready).unwrap_unchecked());
        if (join_waker.is_some()) {
            rstd::move(*join_waker).wake();
        }
    }

    void complete_abort() override {
        auto join_waker = join->complete_abort();
        if (join_waker.is_some()) {
            rstd::move(*join_waker).wake();
        }
    }

    auto submit_completion_facility(FacilityCompletionToken token)
        -> FacilityCompletionSubmitResult override {
        return driver.submit_completion(rstd::move(token));
    }

    auto submit_facility(FacilityTicket ticket, FacilityRequest request)
        -> FacilitySubmitResult override {
        auto token = ticket.into_execution_token();
        if (request.progress() != FacilityProgress::ExternallyDriven ||
            request.effect() != FacilityEffect::ExecuteTaskSegment) {
            auto task = token.access_task();
            if (task.is_some()) {
                (*task)->cancel_facility_handoff(rstd::move(token));
            }
            return FacilitySubmitResult::Unsupported;
        }

        auto endpoint = request.take_endpoint();
        auto job      = rstd::async::FacilityJob { rstd::move(token) };
        switch (endpoint.submit(rstd::move(job))) {
        case rstd::async::FacilityEndpointSubmitResult::Accepted:
            return FacilitySubmitResult::Accepted;
        case rstd::async::FacilityEndpointSubmitResult::Rejected:
            return FacilitySubmitResult::Rejected;
        case rstd::async::FacilityEndpointSubmitResult::Unsupported:
            return FacilitySubmitResult::Unsupported;
        }
        rstd::unreachable();
    }

    void run_facility_execution(FacilityExecutionToken token, TaskAccess access) override {
        auto rt = runtime.upgrade();
        if (! rt) {
            return;
        }
        auto acquired = begin_facility_execution(rstd::move(token), rstd::move(access));
        if (acquired.is_none()) {
            return;
        }
        auto  lease = rstd::move(acquired).unwrap_unchecked();
        auto& self  = lease.task();

        auto scope  = RuntimeScope { *rt.as_ptr() };
        auto domain = ExecutionDomainScope { rstd::async::ExecutionDomainKind::ExternalExecutor };
        auto waker  = make_task_waker(self);
        auto cx     = task::Context { waker };
        auto out    = driver.resume_external_segment(cx);
        if (out.is_submit_facility()) {
            auto outcome = TaskPollAction::submit_facility(out.take_endpoint());
            auto action  = end_facility_execution(rstd::move(lease), rstd::move(outcome));
            apply(rstd::move(action));
            return;
        }
        auto action = end_facility_execution(rstd::move(lease), TaskPollAction::none());
        apply(rstd::move(action));
    }
};

template<typename State>
class HeapTaskStorage {
    static void destroy(voidp owner) { delete static_cast<HeapTaskStorage*>(owner); }

    TaskRefControl control;
    State          state;

public:
    template<typename... Args>
    explicit HeapTaskStorage(Args&&... args)
        : control(this, &destroy), state(control, rstd::forward<Args>(args)...) {}

    auto into_task() -> TaskRef { return TaskRef::adopt(rstd::addressof(control)); }
};

template<typename State>
class ScopedTaskStorage {
    TaskRefControl* control;
    TaskRef         owner;
    State           state;

public:
    template<typename... Args>
    explicit ScopedTaskStorage(Args&&... args)
        : control(TaskRefControl::make_scoped()),
          owner(TaskRef::adopt(control)),
          state(*control, rstd::forward<Args>(args)...) {}

    ScopedTaskStorage(const ScopedTaskStorage&)                    = delete;
    auto operator=(const ScopedTaskStorage&) -> ScopedTaskStorage& = delete;
    ScopedTaskStorage(ScopedTaskStorage&&)                         = delete;
    auto operator=(ScopedTaskStorage&&) -> ScopedTaskStorage&      = delete;

    ~ScopedTaskStorage() { control->detach(); }

    auto task() const -> TaskRef { return owner.clone(); }
    void spawn(RuntimeInner& runtime) { runtime.spawn(owner.clone()); }
};

template<typename T>
auto spawn_driver_on(RuntimeInner& runtime, rstd::async::RuntimeCoroDriver<T> driver)
    -> rstd::async::JoinHandle<T> {
    auto join    = sync::Arc<JoinState<T>>::make();
    auto storage = new HeapTaskStorage<DriverTaskState<T>>(
        runtime.weak(), rstd::move(driver), JoinStateOwner<T>::owned(join.clone()));
    auto task = storage->into_task();
    join->set_task(task.clone());
    runtime.spawn(rstd::move(task));

    return JoinHandleFactory::make<T>(rstd::move(join));
}

namespace rstd::async
{

export template<AwaitableInput A>
auto spawn(A awaitable) -> JoinHandle<await_output_t<A>> {
    auto* runtime = CURRENT_RUNTIME;
    if (runtime == nullptr) {
        rstd::panic { "spawn called without an async runtime" };
    }
    auto driver = make_runtime_driver(into_coro(rstd::move(awaitable)));
    return spawn_driver_on(*runtime, rstd::move(driver));
}

export template<AwaitableInput A>
auto spawn_local(A awaitable) -> JoinHandle<await_output_t<A>> {
    auto* runtime = CURRENT_RUNTIME;
    if (runtime == nullptr) {
        rstd::panic { "spawn_local called without an async runtime" };
    }
    if (runtime->is_thread_pool()) {
        rstd::panic { "spawn_local is only supported by current-thread runtime" };
    }
    auto driver = make_runtime_driver(into_coro(rstd::move(awaitable)));
    return spawn_driver_on(*runtime, rstd::move(driver));
}

} // namespace rstd::async

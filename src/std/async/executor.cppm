export module rstd:async.executor;
export import :async.forward;
import :async.awaitable;
import :async.facility;
import :async.runtime_core;
import rstd.alloc;
import :sync;

using namespace rstd;
using ::alloc::boxed::Box;
using ::alloc::vec::Vec;

namespace rstd::async
{

enum class ExecutorJobAction
{
    Run,
    Cancel,
};

using ExecutorJobFn = Box<dyn<FnMut<void(ExecutorJobAction)>>>;

export class ExecutorJob {
    Option<ExecutorJobFn> m_dispatch;

    explicit ExecutorJob(ExecutorJobFn dispatch): m_dispatch(Some(rstd::move(dispatch))) {}

public:
    ExecutorJob(const ExecutorJob&)                    = delete;
    auto operator=(const ExecutorJob&) -> ExecutorJob& = delete;

    ExecutorJob(ExecutorJob&& other) noexcept: m_dispatch(other.m_dispatch.take()) {}

    auto operator=(ExecutorJob&& other) noexcept -> ExecutorJob& {
        if (this != &other) {
            cancel();
            m_dispatch = other.m_dispatch.take();
        }
        return *this;
    }

    ~ExecutorJob() { cancel(); }

    template<typename F>
    static auto make(F run) -> ExecutorJob {
        return dispatch([run = rstd::move(run)](ExecutorJobAction action) mutable {
            if (action == ExecutorJobAction::Run) {
                run();
            }
        });
    }

    template<typename F, typename C>
    static auto make(F run, C cancel) -> ExecutorJob {
        return dispatch(
            [run = rstd::move(run), cancel = rstd::move(cancel)](ExecutorJobAction action) mutable {
                if (action == ExecutorJobAction::Run) {
                    run();
                } else {
                    cancel();
                }
            });
    }

    template<typename F>
    static auto dispatch(F dispatch) -> ExecutorJob {
        return ExecutorJob { ExecutorJobFn::make(rstd::move(dispatch)) };
    }

    void run() {
        if (m_dispatch.is_none()) {
            return;
        }
        auto dispatch = m_dispatch.take().unwrap_unchecked();
        dispatch->operator()(ExecutorJobAction::Run);
    }

    void cancel() {
        if (m_dispatch.is_none()) {
            return;
        }
        auto dispatch = m_dispatch.take().unwrap_unchecked();
        dispatch->operator()(ExecutorJobAction::Cancel);
    }
};

export struct Executor {
    template<class Self, class Delegate = void>
    struct Api {
        using Trait = Executor;

        // Returning true transfers the job to executor ownership. An accepted job
        // must be run or canceled; dropping it performs cancellation automatically.
        auto post_job(ExecutorJob job) -> bool { return trait_call<0>(this, rstd::move(job)); }
        auto post(ExecutorJob job) -> bool { return post_job(rstd::move(job)); }
        // A closed executor rejects new posts; it may still have accepted jobs to drain.
        auto is_closed() -> bool { return trait_call<1>(this); }
    };

    template<typename Self>
    using Funcs = TraitFuncs<&Self::post_job, &Self::is_closed>;
};

export struct ExecutorContext {
    template<class Self, class Delegate = void>
    struct Api {
        using Trait         = ExecutorContext;
        using executor_type = typename Self::executor_type;

        auto executor() -> executor_type { return trait_call<0>(this); }
    };

    template<typename Self>
        requires requires { typename Self::executor_type; }
    using Funcs = TraitFuncs<&Self::executor>;
};

template<typename E>
struct ExecutorAwaitShared {
    E                         executor;
    sync::Mutex<Option<bool>> result;

    explicit ExecutorAwaitShared(E executor)
        : executor(rstd::move(executor)), result(Option<bool> {}) {}

    auto post_job(ExecutorJob job) -> bool {
        return as<Executor>(executor).post_job(rstd::move(job));
    }

    auto is_closed() -> bool { return as<Executor>(executor).is_closed(); }

    auto load_result() -> Option<bool> {
        auto value = result.lock().unwrap_unchecked();
        return *value;
    }

    void store_result(bool value) {
        auto current = result.lock().unwrap_unchecked();
        *current     = Some(value);
    }
};

template<typename E>
auto executor_endpoint_submit(voidp data, FacilityJob job) -> FacilityEndpointSubmitResult {
    using Shared    = ExecutorAwaitShared<E>;
    auto shared     = sync::Arc<Shared>::from_raw(::alloc::sync::ArcRaw<Shared>::from_raw(data));
    auto job_shared = shared.clone();
    auto accepted   = shared->post_job(ExecutorJob::dispatch(
        [shared = rstd::move(job_shared), job = rstd::move(job)](ExecutorJobAction action) mutable {
            if (action == ExecutorJobAction::Run) {
                job.run();
            } else {
                shared->store_result(false);
                job.cancel();
            }
        }));
    (void)rstd::move(shared).into_raw().into_raw();
    return accepted ? FacilityEndpointSubmitResult::Accepted
                    : FacilityEndpointSubmitResult::Rejected;
}

template<typename E>
void executor_endpoint_drop(voidp data) {
    using Shared = ExecutorAwaitShared<E>;
    auto shared  = sync::Arc<Shared>::from_raw(::alloc::sync::ArcRaw<Shared>::from_raw(data));
    (void)shared;
}

template<typename E>
auto executor_endpoint_vtable() -> const RawFacilityEndpointVTable* {
    static const auto vtable = RawFacilityEndpointVTable {
        &executor_endpoint_submit<E>,
        &executor_endpoint_drop<E>,
    };
    return rstd::addressof(vtable);
}

template<typename E>
auto make_executor_endpoint(const sync::Arc<ExecutorAwaitShared<E>>& shared) -> FacilityEndpoint {
    auto id    = reinterpret_cast<usize>(shared.as_ptr().as_raw_ptr());
    auto owned = shared.clone();
    return FacilityEndpoint::from_raw(
        id,
        RawFacilityEndpoint::from_raw_parts(rstd::move(owned).into_raw().into_raw(),
                                            executor_endpoint_vtable<E>()));
}

template<typename E>
class ExecutorAwaitOperation {
    sync::Arc<ExecutorAwaitShared<E>> m_shared;

public:
    using Output = bool;

    explicit ExecutorAwaitOperation(E executor)
        : m_shared(sync::Arc<ExecutorAwaitShared<E>>::make(rstd::move(executor))) {}

    auto advance(AwaitContext& cx) -> AwaitTransition {
        auto current = m_shared->load_result();
        if (current.is_some()) {
            if (! *current && cx.execution_domain() == ExecutionDomainKind::ExternalExecutor) {
                return AwaitTransition::return_to_owner();
            }
            return AwaitTransition::continue_();
        }
        auto result = ! m_shared->is_closed();
        m_shared->store_result(result);
        if (! result) {
            return cx.execution_domain() == ExecutionDomainKind::ExternalExecutor
                       ? AwaitTransition::return_to_owner()
                       : AwaitTransition::continue_();
        }
        return AwaitTransition::submit_facility(make_executor_endpoint(m_shared));
    }

    auto take_output() -> bool {
        auto result = m_shared->load_result();
        if (result.is_none()) {
            rstd::panic { "executor await output taken before facility completion" };
        }
        return *result;
    }
};

struct LocalExecutorState {
    sync::Mutex<Vec<ExecutorJob>> jobs;
    sync::Mutex<bool>             closed;

    LocalExecutorState(): jobs(Vec<ExecutorJob>::make()), closed(false) {}

    auto post(ExecutorJob job) -> bool {
        auto is_closed = closed.lock().unwrap_unchecked();
        if (*is_closed) {
            return false;
        }
        auto queue = jobs.lock().unwrap_unchecked();
        queue->push(rstd::move(job));
        return true;
    }

    auto take_ready() -> Vec<ExecutorJob> {
        auto queue = jobs.lock().unwrap_unchecked();
        auto out   = rstd::move(*queue);
        *queue     = Vec<ExecutorJob>::make();
        return out;
    }

    auto run_ready() -> usize {
        auto ready = take_ready();
        auto ran   = usize {};
        while (! ready.is_empty()) {
            auto job = ready.remove(0);
            job.run();
            ++ran;
        }
        return ran;
    }

    void close() {
        auto is_closed = closed.lock().unwrap_unchecked();
        *is_closed     = true;
    }

    auto is_closed() -> bool {
        auto is_closed = closed.lock().unwrap_unchecked();
        return *is_closed;
    }
};

export class LocalExecutor {
    sync::Weak<LocalExecutorState> m_state;

    explicit LocalExecutor(sync::Weak<LocalExecutorState> state): m_state(rstd::move(state)) {}

    friend class LocalExecutorContext;

public:
    LocalExecutor(const LocalExecutor&)                        = delete;
    auto operator=(const LocalExecutor&) -> LocalExecutor&     = delete;
    LocalExecutor(LocalExecutor&&) noexcept                    = default;
    auto operator=(LocalExecutor&&) noexcept -> LocalExecutor& = default;
    ~LocalExecutor()                                           = default;

    auto clone() const -> LocalExecutor { return LocalExecutor { m_state.clone() }; }

    auto post_job(ExecutorJob job) -> bool {
        auto state = m_state.upgrade();
        if (! state) {
            return false;
        }
        return state->post(rstd::move(job));
    }

    template<typename F>
    auto post(F job) -> bool {
        return post_job(ExecutorJob::make(rstd::move(job)));
    }

    auto is_closed() -> bool {
        auto state = m_state.upgrade();
        return ! state || state->is_closed();
    }
};

export class LocalExecutorContext {
    sync::Arc<LocalExecutorState> m_state;

public:
    using executor_type = LocalExecutor;

    LocalExecutorContext(): m_state(sync::Arc<LocalExecutorState>::make()) {}

    LocalExecutorContext(const LocalExecutorContext&)                        = delete;
    auto operator=(const LocalExecutorContext&) -> LocalExecutorContext&     = delete;
    LocalExecutorContext(LocalExecutorContext&&) noexcept                    = default;
    auto operator=(LocalExecutorContext&&) noexcept -> LocalExecutorContext& = default;
    ~LocalExecutorContext()                                                  = default;

    static auto make() -> LocalExecutorContext { return LocalExecutorContext {}; }

    auto executor() -> LocalExecutor { return LocalExecutor { m_state.downgrade() }; }

    auto run_ready() -> usize {
        if (! m_state) {
            return 0;
        }
        return m_state->run_ready();
    }

    void close() {
        if (m_state) {
            m_state->close();
        }
    }

    auto is_closed() const -> bool { return ! m_state || m_state->is_closed(); }
};

} // namespace rstd::async

namespace rstd
{

template<>
struct Impl<async::Executor, async::LocalExecutor>
    : LinkClassMethod<async::Executor, async::LocalExecutor> {};

template<>
struct Impl<async::ExecutorContext, async::LocalExecutorContext>
    : LinkClassMethod<async::ExecutorContext, async::LocalExecutorContext> {};

} // namespace rstd

namespace rstd::async
{

struct AnyExecutorInner {
    Box<dyn<Executor>> executor;

    explicit AnyExecutorInner(Box<dyn<Executor>> executor): executor(rstd::move(executor)) {}

    auto post_job(ExecutorJob job) -> bool { return executor->post(rstd::move(job)); }

    auto is_closed() -> bool { return executor->is_closed(); }
};

export class AnyExecutor {
    sync::Arc<AnyExecutorInner> m_inner;

    explicit AnyExecutor(sync::Arc<AnyExecutorInner> inner): m_inner(rstd::move(inner)) {}

public:
    template<typename E>
        requires Impled<E, Executor>
    static auto from_executor(E executor) -> AnyExecutor {
        return AnyExecutor { sync::Arc<AnyExecutorInner>::make(
            Box<dyn<Executor>>::make(rstd::move(executor))) };
    }

    auto clone() const -> AnyExecutor { return AnyExecutor { m_inner.clone() }; }

    auto post_job(ExecutorJob job) -> bool {
        if (! m_inner) {
            return false;
        }
        return m_inner->post_job(rstd::move(job));
    }

    template<typename F>
    auto post(F job) -> bool {
        return post_job(ExecutorJob::make(rstd::move(job)));
    }

    auto is_closed() -> bool { return ! m_inner || m_inner->is_closed(); }
};

template<typename E>
auto make_executor_suspension(E executor) {
    auto operation = ExecutorAwaitOperation<E> { rstd::move(executor) };
    return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
}

template<>
struct AwaitableTraits<LocalExecutor> {
    using Output = bool;

    static auto make_suspension(LocalExecutor& executor) {
        return make_executor_suspension(executor.clone());
    }

    static auto make_suspension(LocalExecutor&& executor) {
        return make_executor_suspension(rstd::move(executor));
    }
};

template<>
struct AwaitableTraits<AnyExecutor> {
    using Output = bool;

    static auto make_suspension(AnyExecutor& executor) {
        return make_executor_suspension(executor.clone());
    }

    static auto make_suspension(AnyExecutor&& executor) {
        return make_executor_suspension(rstd::move(executor));
    }
};

} // namespace rstd::async

namespace rstd
{

template<>
struct Impl<async::Executor, async::AnyExecutor>
    : LinkClassMethod<async::Executor, async::AnyExecutor> {};

} // namespace rstd

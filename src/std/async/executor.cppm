export module rstd:async.executor;
export import :async.forward;
import :async.awaitable;
import rstd.alloc;
import :sync;

using namespace rstd;
using ::alloc::boxed::Box;
using ::alloc::vec::Vec;

namespace rstd::async
{

export using ExecutorJob = Box<dyn<FnMut<void()>>>;

export struct Executor {
    template<class Self, class Delegate = void>
    struct Api {
        using Trait = Executor;

        auto post_job(ExecutorJob job) -> bool {
            return trait_call<0>(this, rstd::move(job));
        }
        auto post(ExecutorJob job) -> bool { return post_job(rstd::move(job)); }
        auto is_closed() -> bool { return trait_call<1>(this); }
    };

    template<typename Self>
    using Funcs = TraitFuncs<&Self::post_job, &Self::is_closed>;
};

export struct ExecutorContext {
    template<class Self, class Delegate = void>
    struct Api {
        using Trait = ExecutorContext;

        auto executor() -> typename Self::executor_type {
            return trait_call<0>(this);
        }
    };

    template<typename Self>
    using Funcs = TraitFuncs<&Self::executor>;
};

struct ExecutorAwaitState {
    sync::atomic::Atomic<bool> ready { false };
};

template<typename E>
struct ExecutorAwaitShared {
    E executor;

    explicit ExecutorAwaitShared(E executor) : executor(rstd::move(executor)) {}

    auto post_job(ExecutorJob job) -> bool {
        return as<Executor>(executor).post_job(rstd::move(job));
    }

    auto is_closed() -> bool {
        return as<Executor>(executor).is_closed();
    }
};

template<typename E>
class ExecutorAwaitOperation {
    sync::Arc<ExecutorAwaitShared<E>> m_shared;
    sync::Arc<ExecutorAwaitState>     m_state;
    bool                              m_posted { false };
    bool                              m_completed { false };
    bool                              m_result { false };

public:
    using Output = bool;

    explicit ExecutorAwaitOperation(E executor)
        : m_shared(sync::Arc<ExecutorAwaitShared<E>>::make(rstd::move(executor))),
          m_state(sync::Arc<ExecutorAwaitState>::make()) {}

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        if (m_completed) {
            return AwaitOperationState::Ready;
        }

        if (m_state->ready.load(sync::atomic::Ordering::Acquire)) {
            m_result    = true;
            m_completed = true;
            return AwaitOperationState::Ready;
        }

        if (! m_posted) {
            m_posted       = true;
            auto state     = m_state.clone();
            auto waker     = cx.waker().clone();
            auto shared    = m_shared.clone();
            auto posted    = shared->post_job(ExecutorJob::make(
                [state = rstd::move(state), waker = rstd::move(waker)]() mutable {
                    state->ready.store(true, sync::atomic::Ordering::Release);
                    rstd::move(waker).wake();
                }));

            if (! posted) {
                m_result    = false;
                m_completed = true;
                return AwaitOperationState::Ready;
            }
        }

        return AwaitOperationState::Pending;
    }

    auto resume_external(AwaitContext& cx) -> AwaitOperationState {
        if (m_completed) {
            return AwaitOperationState::Ready;
        }
        if (cx.execution_domain() != ExecutionDomainKind::ExternalExecutor) {
            return AwaitOperationState::Pending;
        }

        m_result    = ! m_shared->is_closed();
        m_completed = true;
        return AwaitOperationState::Ready;
    }

    auto placement() -> ResumePlacement {
        if (! m_result) {
            return ResumePlacement::runtime_worker();
        }

        auto shared = m_shared.clone();
        auto* self  = this;
        auto post   = ResumePost::make(
            [shared = rstd::move(shared)](ResumeJob job) mutable -> bool {
                return shared->post_job(rstd::move(job));
            });
        auto reject = ResumeReject::make([self]() mutable {
            self->m_result    = false;
            self->m_completed = true;
        });
        return ResumePlacement::external_executor(rstd::move(post), rstd::move(reject));
    }

    auto take_output() const noexcept -> bool { return m_result; }
};

struct LocalExecutorState {
    sync::Mutex<Vec<ExecutorJob>> jobs;
    sync::Mutex<bool>             closed;

    LocalExecutorState() : jobs(Vec<ExecutorJob>::make()), closed(false) {}

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
            job->operator()();
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

    explicit LocalExecutor(sync::Weak<LocalExecutorState> state) : m_state(rstd::move(state)) {}

    friend class LocalExecutorContext;

public:
    LocalExecutor(const LocalExecutor&)            = delete;
    auto operator=(const LocalExecutor&) -> LocalExecutor& = delete;
    LocalExecutor(LocalExecutor&&) noexcept                    = default;
    auto operator=(LocalExecutor&&) noexcept -> LocalExecutor& = default;
    ~LocalExecutor()                                          = default;

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

    LocalExecutorContext() : m_state(sync::Arc<LocalExecutorState>::make()) {}

    LocalExecutorContext(const LocalExecutorContext&)            = delete;
    auto operator=(const LocalExecutorContext&) -> LocalExecutorContext& = delete;
    LocalExecutorContext(LocalExecutorContext&&) noexcept                    = default;
    auto operator=(LocalExecutorContext&&) noexcept -> LocalExecutorContext& = default;
    ~LocalExecutorContext()                                                = default;

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

    explicit AnyExecutorInner(Box<dyn<Executor>> executor) : executor(rstd::move(executor)) {}

    auto post_job(ExecutorJob job) -> bool { return executor->post(rstd::move(job)); }

    auto is_closed() -> bool { return executor->is_closed(); }
};

export class AnyExecutor {
    sync::Arc<AnyExecutorInner> m_inner;

    explicit AnyExecutor(sync::Arc<AnyExecutorInner> inner) : m_inner(rstd::move(inner)) {}

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

export module rstd:async.awaitable;
export import :async.forward;
import rstd.alloc;

using namespace rstd;
using ::alloc::boxed::Box;

namespace rstd::async
{

enum class ExecutionDomainKind
{
    RuntimeWorker,
    ExternalExecutor,
};

class AwaitContext {
    task::Context*      m_poll_context;
    ExecutionDomainKind m_execution_domain;

public:
    explicit constexpr AwaitContext(
        task::Context&      cx,
        ExecutionDomainKind execution_domain = ExecutionDomainKind::RuntimeWorker) noexcept
        : m_poll_context(rstd::addressof(cx)), m_execution_domain(execution_domain) {}

    constexpr auto poll_context() const noexcept -> task::Context& { return *m_poll_context; }
    constexpr auto execution_domain() const noexcept -> ExecutionDomainKind {
        return m_execution_domain;
    }
    constexpr auto waker() const noexcept -> const task::Waker& { return m_poll_context->waker(); }
};

export template<typename A>
using into_future_t = typename mtp::rm_cvf<A>::Future;

enum class ResumePlacementKind
{
    RuntimeWorker,
    ExternalExecutor,
};

using ResumeJob    = Box<dyn<FnMut<void()>>>;
using ResumeCancel = Box<dyn<FnMut<void()>>>;
using ResumePost   = Box<dyn<FnMut<bool(ResumeJob, ResumeCancel)>>>;
using ResumeReject = Box<dyn<FnMut<void()>>>;

struct ResumePlacement {
    ResumePlacementKind  kind { ResumePlacementKind::RuntimeWorker };
    Option<ResumePost>   external_post;
    Option<ResumeReject> external_reject;

    ResumePlacement() = default;

    explicit ResumePlacement(ResumePlacementKind kind)
        : kind(kind), external_post(None()), external_reject(None()) {}

    explicit ResumePlacement(ResumePost post, ResumeReject reject)
        : kind(ResumePlacementKind::ExternalExecutor),
          external_post(Some(rstd::move(post))),
          external_reject(Some(rstd::move(reject))) {}

    ResumePlacement(const ResumePlacement&)                        = delete;
    auto operator=(const ResumePlacement&) -> ResumePlacement&     = delete;
    ResumePlacement(ResumePlacement&&) noexcept                    = default;
    auto operator=(ResumePlacement&&) noexcept -> ResumePlacement& = default;
    ~ResumePlacement()                                             = default;

    static auto runtime_worker() -> ResumePlacement {
        return ResumePlacement { ResumePlacementKind::RuntimeWorker };
    }

    static auto external_executor(ResumePost post, ResumeReject reject) -> ResumePlacement {
        return ResumePlacement { rstd::move(post), rstd::move(reject) };
    }

    auto is_runtime_worker() const noexcept -> bool {
        return kind == ResumePlacementKind::RuntimeWorker;
    }

    auto is_external_executor() const noexcept -> bool {
        return kind == ResumePlacementKind::ExternalExecutor;
    }

    auto post_external(ResumeJob job, ResumeCancel owner_cancel) -> bool {
        auto reject = external_reject.take();
        auto cancel = ResumeCancel::make(
            [reject = rstd::move(reject), owner_cancel = rstd::move(owner_cancel)]() mutable {
                if (reject.is_some()) {
                    auto callback = reject.take().unwrap_unchecked();
                    callback->operator()();
                }
                owner_cancel->operator()();
            });

        if (external_post.is_none()) {
            cancel->operator()();
            return false;
        }

        auto post = external_post.take().unwrap_unchecked();
        return post->operator()(rstd::move(job), rstd::move(cancel));
    }
};

enum class AwaitOperationState
{
    Pending,
    Ready,
};

template<typename A>
struct AwaitableTraits;

template<typename A>
concept InternalAwaitable = requires { typename AwaitableTraits<mtp::rm_cvf<A>>::Output; };

export struct IntoFuture {
    template<class Self, class Delegate = void>
    struct Api {
        using Trait  = IntoFuture;
        using Future = typename Self::Future;

        auto into_future() -> Future { return trait_call<0>(this); }
    };

    template<typename F>
    using Funcs = TraitFuncs<&F::into_future>;
};

template<typename A>
concept FutureInput = requires { typename future::future_output_t<A>; } &&
                      Impled<mtp::rm_cvf<A>, future::Future<future::future_output_t<A>>>;

template<typename A>
concept IntoFutureInput = (! FutureInput<A>) && requires { typename into_future_t<A>; } &&
                          Impled<mtp::rm_cvf<A>, IntoFuture> && FutureInput<into_future_t<A>>;

template<typename A>
concept IntoFutureInClass = requires(mtp::rm_cvf<A>& awaitable) {
    typename into_future_t<A>;
    { awaitable.into_future() } -> mtp::same_as<into_future_t<A>>;
};

} // namespace rstd::async

namespace rstd
{

export template<typename A>
    requires async::IntoFutureInClass<A>
struct Impl<async::IntoFuture, A> : LinkClassMethod<async::IntoFuture, A> {};

} // namespace rstd

namespace rstd::async
{

template<typename A>
struct AwaitOutput {
    using type = future::future_output_t<A>;
};

template<InternalAwaitable A>
struct AwaitOutput<A> {
    using type = typename AwaitableTraits<mtp::rm_cvf<A>>::Output;
};

template<IntoFutureInput A>
struct AwaitOutput<A> {
    using type = future::future_output_t<into_future_t<A>>;
};

export template<typename A>
using await_output_t = typename AwaitOutput<A>::type;

export template<typename A>
concept AwaitableInput = InternalAwaitable<A> || FutureInput<A> || IntoFutureInput<A>;

template<typename Operation, typename Output = typename mtp::rm_cvf<Operation>::Output>
class AwaitSuspension {
    Operation m_operation;

public:
    explicit AwaitSuspension(Operation&& operation)
        : m_operation(rstd::forward<Operation>(operation)) {}

    constexpr auto await_ready() const noexcept -> bool { return false; }

    template<typename Promise>
    void await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle.promise().set_awaiting(this);
    }

    auto await_resume() -> Output { return m_operation.take_output(); }

    auto resume(AwaitContext& cx) -> AwaitOperationState { return m_operation.resume(cx); }

    auto resume_external(AwaitContext& cx) -> AwaitOperationState {
        if constexpr (requires(Operation& operation, AwaitContext& context) {
                          {
                              operation.resume_external(context)
                          } -> mtp::same_as<AwaitOperationState>;
                      }) {
            return m_operation.resume_external(cx);
        } else {
            return AwaitOperationState::Pending;
        }
    }

    auto placement() -> ResumePlacement { return m_operation.placement(); }
};

template<typename Operation>
class AwaitSuspension<Operation, void> {
    Operation m_operation;

public:
    explicit AwaitSuspension(Operation&& operation)
        : m_operation(rstd::forward<Operation>(operation)) {}

    constexpr auto await_ready() const noexcept -> bool { return false; }

    template<typename Promise>
    void await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle.promise().set_awaiting(this);
    }

    void await_resume() { m_operation.take_output(); }

    auto resume(AwaitContext& cx) -> AwaitOperationState { return m_operation.resume(cx); }

    auto resume_external(AwaitContext& cx) -> AwaitOperationState {
        if constexpr (requires(Operation& operation, AwaitContext& context) {
                          {
                              operation.resume_external(context)
                          } -> mtp::same_as<AwaitOperationState>;
                      }) {
            return m_operation.resume_external(cx);
        } else {
            return AwaitOperationState::Pending;
        }
    }

    auto placement() -> ResumePlacement { return m_operation.placement(); }
};

template<typename F, typename T = future::future_output_t<F>>
class FuturePollOperation {
    F         m_future;
    Option<T> m_result;

public:
    using Output = T;

    explicit FuturePollOperation(F&& future): m_future(rstd::forward<F>(future)) {}

    auto take_output() -> Output { return rstd::move(m_result).unwrap_unchecked(); }

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        auto out = future::poll(m_future, cx.poll_context());
        if (out.is_pending()) {
            return AwaitOperationState::Pending;
        }
        m_result.insert(rstd::move(out).take());
        return AwaitOperationState::Ready;
    }

    auto placement() const -> ResumePlacement { return ResumePlacement::runtime_worker(); }
};

template<typename F>
class FuturePollOperation<F, void> {
    F m_future;

public:
    using Output = void;

    explicit FuturePollOperation(F&& future): m_future(rstd::forward<F>(future)) {}

    void take_output() const noexcept {}

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        auto out = future::poll(m_future, cx.poll_context());
        if (out.is_pending()) {
            return AwaitOperationState::Pending;
        }
        rstd::move(out).take();
        return AwaitOperationState::Ready;
    }

    auto placement() const -> ResumePlacement { return ResumePlacement::runtime_worker(); }
};

template<InternalAwaitable A>
auto make_suspension(A&& awaitable) {
    return AwaitableTraits<mtp::rm_cvf<A>>::make_suspension(rstd::forward<A>(awaitable));
}

template<typename F>
    requires FutureInput<F> && (! InternalAwaitable<F>)
auto make_suspension(F&& future) {
    auto operation = FuturePollOperation<F> { rstd::forward<F>(future) };
    return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
}

template<IntoFutureInput A>
auto make_suspension(A&& awaitable) {
    auto future    = as<IntoFuture>(awaitable).into_future();
    auto operation = FuturePollOperation<decltype(future)> { rstd::move(future) };
    return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
}

} // namespace rstd::async

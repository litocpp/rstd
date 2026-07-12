export module rstd:async.awaitable;
export import :async.forward;
import :async.facility;

using namespace rstd;

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

enum class AwaitTransitionKind
{
    Continue,
    Suspend,
    SubmitCompletion,
    SubmitFacility,
    ReturnToOwner,
};

class AwaitTransition {
    AwaitTransitionKind m_kind { AwaitTransitionKind::Suspend };
    FacilityEndpoint    m_endpoint;
    usize               m_facility_id { 0 };

    explicit AwaitTransition(AwaitTransitionKind kind): m_kind(kind) {}

    explicit AwaitTransition(usize facility_id)
        : m_kind(AwaitTransitionKind::SubmitCompletion), m_facility_id(facility_id) {}

    explicit AwaitTransition(FacilityEndpoint endpoint)
        : m_kind(AwaitTransitionKind::SubmitFacility), m_endpoint(rstd::move(endpoint)) {}

public:
    static auto continue_() -> AwaitTransition {
        return AwaitTransition { AwaitTransitionKind::Continue };
    }

    static auto suspend() -> AwaitTransition {
        return AwaitTransition { AwaitTransitionKind::Suspend };
    }

    static auto submit_facility(FacilityEndpoint endpoint) -> AwaitTransition {
        return AwaitTransition { rstd::move(endpoint) };
    }

    static auto submit_completion(usize facility_id) -> AwaitTransition {
        return AwaitTransition { facility_id };
    }

    static auto return_to_owner() -> AwaitTransition {
        return AwaitTransition { AwaitTransitionKind::ReturnToOwner };
    }

    auto kind() const noexcept -> AwaitTransitionKind { return m_kind; }
    auto take_endpoint() -> FacilityEndpoint { return rstd::move(m_endpoint); }
    auto facility_id() const noexcept -> usize { return m_facility_id; }
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

template<typename A>
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

    auto advance(AwaitContext& cx) -> AwaitTransition { return m_operation.advance(cx); }

    template<typename Event>
        requires requires(Operation& operation, Event& event) {
            { operation.complete_facility(event) } -> mtp::same_as<bool>;
        }
    auto complete_facility(Event& event) -> bool {
        return m_operation.complete_facility(event);
    }

    template<typename Token>
        requires requires(Operation& operation, Token token) {
            { operation.submit_completion(rstd::move(token)) };
        }
    auto submit_completion(Token token) {
        return m_operation.submit_completion(rstd::move(token));
    }
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

    auto advance(AwaitContext& cx) -> AwaitTransition { return m_operation.advance(cx); }

    template<typename Event>
        requires requires(Operation& operation, Event& event) {
            { operation.complete_facility(event) } -> mtp::same_as<bool>;
        }
    auto complete_facility(Event& event) -> bool {
        return m_operation.complete_facility(event);
    }

    template<typename Token>
        requires requires(Operation& operation, Token token) {
            { operation.submit_completion(rstd::move(token)) };
        }
    auto submit_completion(Token token) {
        return m_operation.submit_completion(rstd::move(token));
    }
};

template<typename F, typename T = future::future_output_t<F>>
class FuturePollOperation {
    F         m_future;
    Option<T> m_result;

public:
    using Output = T;

    explicit FuturePollOperation(F&& future): m_future(rstd::forward<F>(future)) {}

    auto take_output() -> Output { return rstd::move(m_result).unwrap_unchecked(); }

    auto advance(AwaitContext& cx) -> AwaitTransition {
        if (cx.execution_domain() == ExecutionDomainKind::ExternalExecutor) {
            return AwaitTransition::return_to_owner();
        }
        auto out = future::poll(m_future, cx.poll_context());
        if (out.is_pending()) {
            return AwaitTransition::suspend();
        }
        m_result.insert(rstd::move(out).take());
        return AwaitTransition::continue_();
    }
};

template<typename F>
class FuturePollOperation<F, void> {
    F m_future;

public:
    using Output = void;

    explicit FuturePollOperation(F&& future): m_future(rstd::forward<F>(future)) {}

    void take_output() const noexcept {}

    auto advance(AwaitContext& cx) -> AwaitTransition {
        if (cx.execution_domain() == ExecutionDomainKind::ExternalExecutor) {
            return AwaitTransition::return_to_owner();
        }
        auto out = future::poll(m_future, cx.poll_context());
        if (out.is_pending()) {
            return AwaitTransition::suspend();
        }
        rstd::move(out).take();
        return AwaitTransition::continue_();
    }
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

export module rstd:async.awaitable;
export import :async.forward;

using namespace rstd;

namespace rstd::async
{

class AwaitContext {
    task::Context* m_poll_context;

public:
    explicit constexpr AwaitContext(task::Context& cx) noexcept
        : m_poll_context(rstd::addressof(cx)) {}

    constexpr auto poll_context() const noexcept -> task::Context& { return *m_poll_context; }
    constexpr auto waker() const noexcept -> const task::Waker& {
        return m_poll_context->waker();
    }
};

export template<typename A>
using into_future_t = typename mtp::rm_cvf<A>::Future;

enum class ResumePlacementKind {
    RuntimeWorker,
    ExternalExecutor,
};

struct ResumePlacement {
    ResumePlacementKind kind { ResumePlacementKind::RuntimeWorker };

    static constexpr auto runtime_worker() noexcept -> ResumePlacement {
        return ResumePlacement { ResumePlacementKind::RuntimeWorker };
    }

    static constexpr auto external_executor() noexcept -> ResumePlacement {
        return ResumePlacement { ResumePlacementKind::ExternalExecutor };
    }

    constexpr auto is_runtime_worker() const noexcept -> bool {
        return kind == ResumePlacementKind::RuntimeWorker;
    }
};

enum class AwaitOperationState {
    Pending,
    Ready,
};

template<typename A>
struct AwaitableTraits;

template<typename A>
concept InternalAwaitable = requires {
    typename AwaitableTraits<mtp::rm_cvf<A>>::Output;
};

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
concept FutureInput = requires {
    typename future::future_output_t<A>;
} && Impled<mtp::rm_cvf<A>, future::Future<future::future_output_t<A>>>;

template<typename A>
concept IntoFutureInput = (! FutureInput<A>) &&
    requires {
        typename into_future_t<A>;
    } && Impled<mtp::rm_cvf<A>, IntoFuture> && FutureInput<into_future_t<A>>;

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

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        return m_operation.resume(cx);
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

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        return m_operation.resume(cx);
    }

    auto placement() -> ResumePlacement { return m_operation.placement(); }
};

template<typename F, typename T = future::future_output_t<F>>
class FuturePollOperation {
    F              m_future;
    Option<T>      m_result;

public:
    using Output = T;

    explicit FuturePollOperation(F&& future) : m_future(rstd::forward<F>(future)) {}

    auto take_output() -> Output { return rstd::move(m_result).unwrap_unchecked(); }

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        auto out = future::poll(m_future, cx.poll_context());
        if (out.is_pending()) {
            return AwaitOperationState::Pending;
        }
        m_result.insert(rstd::move(out).take());
        return AwaitOperationState::Ready;
    }

    constexpr auto placement() const noexcept -> ResumePlacement {
        return ResumePlacement::runtime_worker();
    }
};

template<typename F>
class FuturePollOperation<F, void> {
    F m_future;

public:
    using Output = void;

    explicit FuturePollOperation(F&& future) : m_future(rstd::forward<F>(future)) {}

    void take_output() const noexcept {}

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        auto out = future::poll(m_future, cx.poll_context());
        if (out.is_pending()) {
            return AwaitOperationState::Pending;
        }
        rstd::move(out).take();
        return AwaitOperationState::Ready;
    }

    constexpr auto placement() const noexcept -> ResumePlacement {
        return ResumePlacement::runtime_worker();
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
    auto future = as<IntoFuture>(awaitable).into_future();
    auto operation = FuturePollOperation<decltype(future)> { rstd::move(future) };
    return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
}

} // namespace rstd::async

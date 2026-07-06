export module rstd:async.awaitable;
export import :async.forward;

using namespace rstd;

namespace rstd::async
{

class AwaitContext {
    task::Context* m_task_context;

public:
    explicit constexpr AwaitContext(task::Context& cx) noexcept
        : m_task_context(rstd::addressof(cx)) {}

    constexpr auto task_context() const noexcept -> task::Context& { return *m_task_context; }
    constexpr auto waker() const noexcept -> const task::Waker& {
        return m_task_context->waker();
    }
};

export template<typename A>
using into_future_t = decltype(rstd::forward<A>(mtp::declval<A>()).into_future());

enum class AwaitOperationState {
    Pending,
    Ready,
};

template<typename A>
struct AwaitableTraits;

template<typename A>
concept InternalAwaitableLike = requires {
    typename AwaitableTraits<mtp::rm_cvf<A>>::Output;
};

export template<typename A>
concept IntoFutureLike = (! future::FutureLike<A>) &&
    requires(A&& awaitable) {
        rstd::forward<A>(awaitable).into_future();
    } && future::FutureLike<into_future_t<A>>;

template<typename A>
struct AwaitOutput {
    using type = future::future_output_t<A>;
};

template<InternalAwaitableLike A>
struct AwaitOutput<A> {
    using type = typename AwaitableTraits<mtp::rm_cvf<A>>::Output;
};

template<IntoFutureLike A>
struct AwaitOutput<A> {
    using type = future::future_output_t<into_future_t<A>>;
};

export template<typename A>
using await_output_t = typename AwaitOutput<A>::type;

export template<typename A>
concept AwaitableLike = InternalAwaitableLike<A> || future::FutureLike<A> || IntoFutureLike<A>;

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
        auto out = future::poll(m_future, cx.task_context());
        if (out.is_pending()) {
            return AwaitOperationState::Pending;
        }
        m_result.insert(rstd::move(out).take());
        return AwaitOperationState::Ready;
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
        auto out = future::poll(m_future, cx.task_context());
        if (out.is_pending()) {
            return AwaitOperationState::Pending;
        }
        rstd::move(out).take();
        return AwaitOperationState::Ready;
    }
};

template<InternalAwaitableLike A>
auto make_suspension(A&& awaitable) {
    return AwaitableTraits<mtp::rm_cvf<A>>::make_suspension(rstd::forward<A>(awaitable));
}

template<future::FutureLike F>
    requires(! InternalAwaitableLike<F>)
auto make_suspension(F&& future) {
    auto operation = FuturePollOperation<F> { rstd::forward<F>(future) };
    return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
}

template<IntoFutureLike A>
auto make_suspension(A&& awaitable) {
    auto future = rstd::forward<A>(awaitable).into_future();
    auto operation = FuturePollOperation<decltype(future)> { rstd::move(future) };
    return AwaitSuspension<decltype(operation)> { rstd::move(operation) };
}

} // namespace rstd::async

export module rstd:async.task;
export import :async.forward;

using namespace rstd;

struct AwaiterBase {
    virtual auto poll(task::Context& cx) -> bool = 0;

protected:
    ~AwaiterBase() = default;
};

template<typename Promise, typename F, typename Output = future::future_output_t<F>>
struct FutureAwaiter : AwaiterBase {
    F              m_future;
    Option<Output> m_result;

    explicit FutureAwaiter(F&& future) : m_future(rstd::forward<F>(future)) {}

    constexpr auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle.promise().awaiting = this;
    }

    auto await_resume() -> Output { return rstd::move(m_result).unwrap_unchecked(); }

    auto poll(task::Context& cx) -> bool override {
        auto out = future::poll(m_future, cx);
        if (out.is_pending()) {
            return false;
        }
        m_result.insert(rstd::move(out).take());
        return true;
    }
};

template<typename Promise, typename F>
struct FutureAwaiter<Promise, F, void> : AwaiterBase {
    F m_future;

    explicit FutureAwaiter(F&& future) : m_future(rstd::forward<F>(future)) {}

    constexpr auto await_ready() const noexcept -> bool { return false; }

    void await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        handle.promise().awaiting = this;
    }

    void await_resume() const noexcept {}

    auto poll(task::Context& cx) -> bool override {
        auto out = future::poll(m_future, cx);
        if (out.is_pending()) {
            return false;
        }
        rstd::move(out).take();
        return true;
    }
};

namespace rstd::async
{

export template<typename T>
class coro {
public:
    using Output = T;

    struct promise_type {
        AwaiterBase* awaiting { nullptr };
        Option<T>    result;

        auto get_return_object() noexcept -> coro {
            return coro { std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        constexpr auto initial_suspend() const noexcept { return std::suspend_always {}; }
        constexpr auto final_suspend() const noexcept { return std::suspend_always {}; }

        template<typename U>
        void return_value(U&& value) {
            result.insert(rstd::forward<U>(value));
        }

        void unhandled_exception() {
            rstd::panic { "unhandled exception in async coro" };
        }

        template<future::FutureLike F>
        auto await_transform(F&& future) {
            return FutureAwaiter<promise_type, F> { rstd::forward<F>(future) };
        }

        template<typename A>
            requires(! future::FutureLike<A>) && requires(A&& awaitable) {
                rstd::forward<A>(awaitable).into_future();
            }
        auto await_transform(A&& awaitable) {
            auto future = rstd::forward<A>(awaitable).into_future();
            return FutureAwaiter<promise_type, decltype(future)> { rstd::move(future) };
        }

        auto take_result() -> T {
            if (result.is_none()) {
                rstd::panic { "async coro completed without a value" };
            }
            return rstd::move(result).unwrap_unchecked();
        }
    };

private:
    using Handle = std::coroutine_handle<promise_type>;

    Handle m_handle {};
    bool   m_completed { false };

    explicit coro(Handle handle) noexcept : m_handle(handle) {}

public:
    coro(const coro&)            = delete;
    coro& operator=(const coro&) = delete;

    coro(coro&& other) noexcept
        : m_handle(rstd::exchange(other.m_handle, {})), m_completed(other.m_completed) {}

    auto operator=(coro&& other) noexcept -> coro& {
        if (this != &other) {
            if (m_handle) {
                m_handle.destroy();
            }
            m_handle    = rstd::exchange(other.m_handle, {});
            m_completed = other.m_completed;
        }
        return *this;
    }

    ~coro() {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    auto poll(mut_ref<coro> self, task::Context& cx) -> task::Poll<T> {
        auto& task = *self;
        if (! task.m_handle) {
            rstd::panic { "empty async coro polled" };
        }
        if (task.m_completed) {
            rstd::panic { "async coro polled after completion" };
        }

        auto& promise = task.m_handle.promise();
        while (true) {
            if (promise.awaiting != nullptr) {
                auto* awaiting = promise.awaiting;
                if (! awaiting->poll(cx)) {
                    return task::Poll<T>::Pending();
                }
                promise.awaiting = nullptr;
                task.m_handle.resume();
            } else {
                task.m_handle.resume();
            }

            if (task.m_handle.done()) {
                task.m_completed = true;
                return task::Poll<T>::Ready(promise.take_result());
            }
        }
    }
};

template<>
class coro<void> {
public:
    using Output = void;

    struct promise_type {
        AwaiterBase* awaiting { nullptr };

        auto get_return_object() noexcept -> coro {
            return coro { std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        constexpr auto initial_suspend() const noexcept { return std::suspend_always {}; }
        constexpr auto final_suspend() const noexcept { return std::suspend_always {}; }

        constexpr void return_void() const noexcept {}

        void unhandled_exception() {
            rstd::panic { "unhandled exception in async coro" };
        }

        template<future::FutureLike F>
        auto await_transform(F&& future) {
            return FutureAwaiter<promise_type, F> { rstd::forward<F>(future) };
        }

        template<typename A>
            requires(! future::FutureLike<A>) && requires(A&& awaitable) {
                rstd::forward<A>(awaitable).into_future();
            }
        auto await_transform(A&& awaitable) {
            auto future = rstd::forward<A>(awaitable).into_future();
            return FutureAwaiter<promise_type, decltype(future)> { rstd::move(future) };
        }
    };

private:
    using Handle = std::coroutine_handle<promise_type>;

    Handle m_handle {};
    bool   m_completed { false };

    explicit coro(Handle handle) noexcept : m_handle(handle) {}

public:
    coro(const coro&)            = delete;
    coro& operator=(const coro&) = delete;

    coro(coro&& other) noexcept
        : m_handle(rstd::exchange(other.m_handle, {})), m_completed(other.m_completed) {}

    auto operator=(coro&& other) noexcept -> coro& {
        if (this != &other) {
            if (m_handle) {
                m_handle.destroy();
            }
            m_handle    = rstd::exchange(other.m_handle, {});
            m_completed = other.m_completed;
        }
        return *this;
    }

    ~coro() {
        if (m_handle) {
            m_handle.destroy();
        }
    }

    auto poll(mut_ref<coro> self, task::Context& cx) -> task::Poll<void> {
        auto& task = *self;
        if (! task.m_handle) {
            rstd::panic { "empty async coro polled" };
        }
        if (task.m_completed) {
            rstd::panic { "async coro polled after completion" };
        }

        auto& promise = task.m_handle.promise();
        while (true) {
            if (promise.awaiting != nullptr) {
                auto* awaiting = promise.awaiting;
                if (! awaiting->poll(cx)) {
                    return task::Poll<void>::Pending();
                }
                promise.awaiting = nullptr;
                task.m_handle.resume();
            } else {
                task.m_handle.resume();
            }

            if (task.m_handle.done()) {
                task.m_completed = true;
                return task::Poll<void>::Ready();
            }
        }
    }
};

} // namespace rstd::async

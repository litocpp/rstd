export module rstd:async.task;
export import :async.awaitable;

using namespace rstd;

struct AwaitingOperation {
    void* ptr { nullptr };
    async::AwaitOperationState (*resume_fn)(void*, async::AwaitContext&) { nullptr };

    template<typename Suspension>
    static auto make(Suspension* suspension) noexcept -> AwaitingOperation {
        return AwaitingOperation {
            suspension,
            [](void* ptr, async::AwaitContext& cx) -> async::AwaitOperationState {
                return static_cast<Suspension*>(ptr)->resume(cx);
            },
        };
    }

    constexpr explicit operator bool() const noexcept { return ptr != nullptr; }

    auto resume(async::AwaitContext& cx) const -> async::AwaitOperationState {
        return resume_fn(ptr, cx);
    }
};

namespace rstd::async
{

template<typename T>
struct CoroAccess;

export template<typename T>
class coro {
public:
    struct promise_type {
        AwaitingOperation awaiting {};
        Option<T>         result;

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

        template<typename Suspension>
        void set_awaiting(Suspension* suspension) noexcept {
            awaiting = AwaitingOperation::make(suspension);
        }

        template<AwaitableLike A>
        auto await_transform(A&& awaitable) {
            return make_suspension(rstd::forward<A>(awaitable));
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

    explicit coro(Handle handle) noexcept : m_handle(handle) {}

    friend struct CoroAccess<T>;

public:
    coro(const coro&)            = delete;
    coro& operator=(const coro&) = delete;

    coro(coro&& other) noexcept : m_handle(rstd::exchange(other.m_handle, {})) {}

    auto operator=(coro&& other) noexcept -> coro& {
        if (this != &other) {
            if (m_handle) {
                m_handle.destroy();
            }
            m_handle = rstd::exchange(other.m_handle, {});
        }
        return *this;
    }

    ~coro() {
        if (m_handle) {
            m_handle.destroy();
        }
    }
};

template<>
class coro<void> {
public:
    struct promise_type {
        AwaitingOperation awaiting {};

        auto get_return_object() noexcept -> coro {
            return coro { std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        constexpr auto initial_suspend() const noexcept { return std::suspend_always {}; }
        constexpr auto final_suspend() const noexcept { return std::suspend_always {}; }

        constexpr void return_void() const noexcept {}

        void unhandled_exception() {
            rstd::panic { "unhandled exception in async coro" };
        }

        template<typename Suspension>
        void set_awaiting(Suspension* suspension) noexcept {
            awaiting = AwaitingOperation::make(suspension);
        }

        template<AwaitableLike A>
        auto await_transform(A&& awaitable) {
            return make_suspension(rstd::forward<A>(awaitable));
        }
    };

private:
    using Handle = std::coroutine_handle<promise_type>;

    Handle m_handle {};

    explicit coro(Handle handle) noexcept : m_handle(handle) {}

    friend struct CoroAccess<void>;

public:
    coro(const coro&)            = delete;
    coro& operator=(const coro&) = delete;

    coro(coro&& other) noexcept : m_handle(rstd::exchange(other.m_handle, {})) {}

    auto operator=(coro&& other) noexcept -> coro& {
        if (this != &other) {
            if (m_handle) {
                m_handle.destroy();
            }
            m_handle = rstd::exchange(other.m_handle, {});
        }
        return *this;
    }

    ~coro() {
        if (m_handle) {
            m_handle.destroy();
        }
    }
};

template<typename T>
struct CoroAccess {
    static auto handle(coro<T>& task) noexcept -> std::coroutine_handle<typename coro<T>::promise_type>& {
        return task.m_handle;
    }
};

template<typename T>
auto resume_coro(coro<T>& task, bool& completed, task::Context& cx) -> task::Poll<T> {
    auto& handle = CoroAccess<T>::handle(task);
    if (! handle) {
        rstd::panic { "empty async coro resumed" };
    }
    if (completed) {
        rstd::panic { "async coro resumed after completion" };
    }

    auto& promise = handle.promise();
    while (true) {
        auto await_context = async::AwaitContext { cx };
        if (promise.awaiting) {
            auto awaiting = promise.awaiting;
            if (awaiting.resume(await_context) == async::AwaitOperationState::Pending) {
                return task::Poll<T>::Pending();
            }
            promise.awaiting = {};
            handle.resume();
        } else {
            handle.resume();
        }

        if (handle.done()) {
            completed = true;
            if constexpr (mtp::is_void<T>) {
                return task::Poll<void>::Ready();
            } else {
                return task::Poll<T>::Ready(promise.take_result());
            }
        }
    }
}

template<typename T>
class CoroWaitOperation {
public:
    using Output = T;

private:
    coro<T>   m_coro;
    bool      m_completed { false };
    Option<T> m_ready;

public:
    explicit CoroWaitOperation(coro<T>&& task) : m_coro(rstd::move(task)) {}

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        auto out = resume_coro(m_coro, m_completed, cx.task_context());
        if (out.is_pending()) {
            return AwaitOperationState::Pending;
        }
        m_ready.insert(rstd::move(out).take());
        return AwaitOperationState::Ready;
    }

    auto take_output() -> T { return rstd::move(m_ready).unwrap_unchecked(); }
};

template<>
class CoroWaitOperation<void> {
public:
    using Output = void;

private:
    coro<void> m_coro;
    bool       m_completed { false };

public:
    explicit CoroWaitOperation(coro<void>&& task) : m_coro(rstd::move(task)) {}

    auto resume(AwaitContext& cx) -> AwaitOperationState {
        auto out = resume_coro(m_coro, m_completed, cx.task_context());
        if (out.is_pending()) {
            return AwaitOperationState::Pending;
        }
        rstd::move(out).take();
        return AwaitOperationState::Ready;
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

export template<AwaitableLike A>
auto into_coro(A awaitable) -> coro<await_output_t<A>> {
    if constexpr (mtp::is_void<await_output_t<A>>) {
        co_await rstd::move(awaitable);
        co_return;
    } else {
        co_return co_await rstd::move(awaitable);
    }
}

} // namespace rstd::async

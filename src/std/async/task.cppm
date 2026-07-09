export module rstd:async.task;
export import :async.awaitable;

using namespace rstd;

struct AwaitingOperation {
    void* ptr { nullptr };
    async::AwaitOperationState (*resume_fn)(void*, async::AwaitContext&) { nullptr };
    async::AwaitOperationState (*resume_external_fn)(void*, async::AwaitContext&) { nullptr };
    async::ResumePlacement (*placement_fn)(void*) { nullptr };

    template<typename Suspension>
    static auto make(Suspension* suspension) noexcept -> AwaitingOperation {
        return AwaitingOperation {
            suspension,
            [](void* ptr, async::AwaitContext& cx) -> async::AwaitOperationState {
                return static_cast<Suspension*>(ptr)->resume(cx);
            },
            [](void* ptr, async::AwaitContext& cx) -> async::AwaitOperationState {
                return static_cast<Suspension*>(ptr)->resume_external(cx);
            },
            [](void* ptr) -> async::ResumePlacement {
                return static_cast<Suspension*>(ptr)->placement();
            },
        };
    }

    constexpr explicit operator bool() const noexcept { return ptr != nullptr; }

    auto resume(async::AwaitContext& cx) const -> async::AwaitOperationState {
        return resume_fn(ptr, cx);
    }

    auto resume_external(async::AwaitContext& cx) const -> async::AwaitOperationState {
        return resume_external_fn(ptr, cx);
    }

    auto placement() const -> async::ResumePlacement {
        return placement_fn(ptr);
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

        template<AwaitableInput A>
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

        template<AwaitableInput A>
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

export template<AwaitableInput A>
    requires(! mtp::spec_of<mtp::rm_cvf<A>, coro>)
auto into_coro(A awaitable) -> coro<await_output_t<A>> {
    if constexpr (mtp::is_void<await_output_t<A>>) {
        co_await rstd::move(awaitable);
        co_return;
    } else {
        co_return co_await rstd::move(awaitable);
    }
}

export template<typename T>
auto into_coro(coro<T> task) -> coro<T> {
    return rstd::move(task);
}

} // namespace rstd::async

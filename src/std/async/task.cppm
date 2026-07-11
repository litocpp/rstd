export module rstd:async.task;
export import :async.awaitable;
import :async.runtime_core;

using namespace rstd;

struct AwaitingOperation {
    void* ptr { nullptr };
    async::AwaitTransition (*advance_fn)(void*, async::AwaitContext&) { nullptr };
    bool (*complete_facility_fn)(void*, FacilityEvent&) { nullptr };
    FacilityCompletionSubmitResult (*submit_completion_fn)(void*,
                                                           FacilityCompletionToken) { nullptr };

    template<typename Suspension>
    static auto make(Suspension* suspension) noexcept -> AwaitingOperation {
        auto operation = AwaitingOperation {
            suspension,
            [](void* ptr, async::AwaitContext& cx) -> async::AwaitTransition {
                return static_cast<Suspension*>(ptr)->advance(cx);
            },
            nullptr,
            nullptr,
        };
        if constexpr (requires(Suspension& value, FacilityEvent& event) {
                          { value.complete_facility(event) } -> mtp::same_as<bool>;
                      }) {
            operation.complete_facility_fn = [](void* ptr, FacilityEvent& event) -> bool {
                return static_cast<Suspension*>(ptr)->complete_facility(event);
            };
        }
        if constexpr (requires(Suspension& value, FacilityCompletionToken token) {
                          {
                              value.submit_completion(rstd::move(token))
                          } -> mtp::same_as<FacilityCompletionSubmitResult>;
                      }) {
            operation.submit_completion_fn =
                [](void* ptr, FacilityCompletionToken token) -> FacilityCompletionSubmitResult {
                return static_cast<Suspension*>(ptr)->submit_completion(rstd::move(token));
            };
        }
        return operation;
    }

    constexpr explicit operator bool() const noexcept { return ptr != nullptr; }

    auto advance(async::AwaitContext& cx) const -> async::AwaitTransition {
        return advance_fn(ptr, cx);
    }

    auto complete_facility(FacilityEvent& event) const -> bool {
        return complete_facility_fn != nullptr && complete_facility_fn(ptr, event);
    }

    auto submit_completion(FacilityCompletionToken token) const -> FacilityCompletionSubmitResult {
        if (submit_completion_fn == nullptr) {
            return FacilityCompletionSubmitResult::unsupported(rstd::move(token));
        }
        return submit_completion_fn(ptr, rstd::move(token));
    }
};

namespace rstd::async
{

#if defined(__clang__) && __has_cpp_attribute(clang::coro_await_elidable)
#define RSTD_CORO_AWAIT_ELIDABLE [[clang::coro_await_elidable]]
#else
#define RSTD_CORO_AWAIT_ELIDABLE
#endif

template<typename T>
struct CoroAccess;

export template<typename T>
class RSTD_CORO_AWAIT_ELIDABLE coro {
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

        void unhandled_exception() { rstd::panic { "unhandled exception in async coro" }; }

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

    explicit coro(Handle handle) noexcept: m_handle(handle) {}

    friend struct CoroAccess<T>;

public:
    coro(const coro&)            = delete;
    coro& operator=(const coro&) = delete;

    coro(coro&& other) noexcept: m_handle(rstd::exchange(other.m_handle, {})) {}

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
class RSTD_CORO_AWAIT_ELIDABLE coro<void> {
public:
    struct promise_type {
        AwaitingOperation awaiting {};

        auto get_return_object() noexcept -> coro {
            return coro { std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        constexpr auto initial_suspend() const noexcept { return std::suspend_always {}; }
        constexpr auto final_suspend() const noexcept { return std::suspend_always {}; }

        constexpr void return_void() const noexcept {}

        void unhandled_exception() { rstd::panic { "unhandled exception in async coro" }; }

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

    explicit coro(Handle handle) noexcept: m_handle(handle) {}

    friend struct CoroAccess<void>;

public:
    coro(const coro&)            = delete;
    coro& operator=(const coro&) = delete;

    coro(coro&& other) noexcept: m_handle(rstd::exchange(other.m_handle, {})) {}

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

#undef RSTD_CORO_AWAIT_ELIDABLE

template<typename T>
struct CoroAccess {
    static auto handle(coro<T>& task) noexcept
        -> std::coroutine_handle<typename coro<T>::promise_type>& {
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
#if defined(__clang__) && __has_cpp_attribute(clang::coro_await_elidable_argument)
auto into_coro([[clang::coro_await_elidable_argument]] coro<T> task) -> coro<T> {
#else
auto into_coro(coro<T> task) -> coro<T> {
#endif
    return rstd::move(task);
}

} // namespace rstd::async

#include <coroutine>
#include <exception>
#include <gtest/gtest.h>
#include <optional>
#include <rstd/macro.hpp>
#include <string>
#include <utility>
import rstd.core;

using namespace rstd;

namespace
{

template<typename T>
class ImmediateTask {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::optional<T> result;

        auto get_return_object() -> ImmediateTask {
            return ImmediateTask(Handle::from_promise(*this));
        }
        static auto initial_suspend() noexcept -> std::suspend_never { return {}; }
        static auto final_suspend() noexcept -> std::suspend_always { return {}; }

        template<typename U>
        void return_value(U&& value) {
            result.emplace(std::forward<U>(value));
        }

        void unhandled_exception() { std::terminate(); }
    };

private:
    Handle handle_;

    explicit ImmediateTask(Handle handle): handle_(handle) {}

public:
    ImmediateTask(const ImmediateTask&) = delete;
    ImmediateTask(ImmediateTask&& other) noexcept: handle_(std::exchange(other.handle_, {})) {}

    ~ImmediateTask() {
        if (handle_) handle_.destroy();
    }

    auto take() && -> T { return std::move(*handle_.promise().result); }
};

template<typename T>
struct Ready {
    T value;

    auto await_ready() const noexcept -> bool { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    auto await_resume() -> T { return std::move(value); }
};

struct InnerError {
    int value;
};

struct OuterError {
    int value;

    OuterError(InnerError&& error): value(error.value + 10) {}
};

auto child_result(bool success, int& calls) -> Result<int, int> {
    ++calls;
    if (success) return Ok(12);
    return Err(8);
}

auto propagate_coro(bool success, int& calls, int& continued) -> ImmediateTask<Result<long, int>> {
    auto child = co_await Ready { child_result(success, calls) };
    auto value = rstd_co_try(rstd::move(child));
    ++continued;
    co_return Ok(static_cast<long>(value + 1));
}

auto transform_coro_error() -> ImmediateTask<Result<int, std::string>> {
    int  calls = 0;
    auto child = co_await Ready { child_result(false, calls) };
    auto value = rstd_co_try(rstd::move(child), [](int error) {
        return std::to_string(error);
    });
    co_return Ok(value);
}

auto convert_coro_error() -> ImmediateTask<Result<long, OuterError>> {
    auto child = co_await Ready { Result<int, InnerError>(Err(InnerError { 7 })) };
    auto value = rstd_co_try(rstd::move(child));
    co_return Ok(static_cast<long>(value));
}

auto propagate_option_coro(bool success, int& continued) -> ImmediateTask<Option<int>> {
    Option<int> child = success ? Option<int>(Some(4)) : Option<int>(None());
    auto        value = rstd_co_try(rstd::move(child));
    ++continued;
    co_return Some(value + 1);
}

TEST(Try, CoreCoroutineContinuesOnSuccessAndReturnsEarlyOnFailure) {
    int calls     = 0;
    int continued = 0;

    auto success = propagate_coro(true, calls, continued).take();
    ASSERT_TRUE(success.is_ok());
    EXPECT_EQ(success.unwrap(), 13);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(continued, 1);

    auto failure = propagate_coro(false, calls, continued).take();
    ASSERT_TRUE(failure.is_err());
    EXPECT_EQ(failure.unwrap_err(), 8);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(continued, 1);
}

TEST(Try, CoreCoroutineTransformsError) {
    auto result = transform_coro_error().take();

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err(), "8");
}

TEST(Try, CoreCoroutineConvertsErrorThroughPublicConstructor) {
    auto result = convert_coro_error().take();

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().value, 17);
}

TEST(Try, CoreCoroutinePropagatesOption) {
    int continued = 0;

    auto success = propagate_option_coro(true, continued).take();
    ASSERT_TRUE(success.is_some());
    EXPECT_EQ(success.unwrap(), 5);
    EXPECT_EQ(continued, 1);

    auto failure = propagate_option_coro(false, continued).take();
    EXPECT_TRUE(failure.is_none());
    EXPECT_EQ(continued, 1);
}

} // namespace

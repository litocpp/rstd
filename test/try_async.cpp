#include <gtest/gtest.h>
#include <string>
#include <rstd/macro.hpp>
import rstd;

using namespace rstd;

namespace
{

struct InnerError {
    int value;
};

struct OuterError {
    int value;

    OuterError(InnerError&& error): value(error.value + 10) {}
};

auto child_result(bool success, int& calls) -> async::coro<Result<int, int>> {
    ++calls;
    if (success) {
        co_return Ok(12);
    }
    co_return Err(8);
}

auto propagate_coro(bool success, int& calls, int& continued) -> async::coro<Result<long, int>> {
    auto value = rstd_co_try(co_await child_result(success, calls));
    ++continued;
    co_return Ok(static_cast<long>(value + 1));
}

auto transform_coro_error() -> async::coro<Result<int, std::string>> {
    int  calls = 0;
    auto value = rstd_co_try(co_await child_result(false, calls), [](int error) {
        return std::to_string(error);
    });
    co_return Ok(value);
}

auto child_converted_error() -> async::coro<Result<int, InnerError>> {
    co_return Err(InnerError { 7 });
}

auto convert_coro_error() -> async::coro<Result<long, OuterError>> {
    auto value = rstd_co_try(co_await child_converted_error());
    co_return Ok(static_cast<long>(value));
}

TEST(Try, CoroutineContinuesOnSuccessAndReturnsEarlyOnFailure) {
    int calls     = 0;
    int continued = 0;

    auto success = async::block_on(propagate_coro(true, calls, continued));
    ASSERT_TRUE(success.is_ok());
    EXPECT_EQ(success.unwrap(), 13);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(continued, 1);

    auto failure = async::block_on(propagate_coro(false, calls, continued));
    ASSERT_TRUE(failure.is_err());
    EXPECT_EQ(failure.unwrap_err(), 8);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(continued, 1);
}

TEST(Try, CoroutineTransformsError) {
    auto result = async::block_on(transform_coro_error());

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err(), "8");
}

TEST(Try, CoroutineConvertsErrorThroughPublicConstructor) {
    auto result = async::block_on(convert_coro_error());

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().value, 17);
}

} // namespace

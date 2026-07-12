#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <rstd/macro.hpp>
import rstd.core;
import try_module_check;

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

struct CallableError {
    int* calls;

    auto operator()(int error) -> int {
        ++*calls;
        return error + 20;
    }
};

struct DropProbe {
    int* drops;

    explicit DropProbe(int& drops): drops(&drops) {}
    DropProbe(const DropProbe&)            = delete;
    DropProbe& operator=(const DropProbe&) = delete;
    DropProbe(DropProbe&& other) noexcept: drops(rstd::exchange(other.drops, nullptr)) {}
    DropProbe& operator=(DropProbe&&) = delete;
    ~DropProbe() {
        if (drops != nullptr) {
            ++*drops;
        }
    }
};

auto make_result(bool success, int& calls) -> Result<int, int> {
    ++calls;
    if (success) {
        return Ok(7);
    }
    return Err(5);
}

auto propagate_result(bool success, int& calls, int& continued) -> Result<long, int> {
    auto value = rstd_try(make_result(success, calls));
    ++continued;
    return Ok(static_cast<long>(value + 1));
}

auto convert_error() -> Result<long, OuterError> {
    auto value = rstd_try((Result<int, InnerError> { Err(InnerError { 4 }) }));
    return Ok(static_cast<long>(value));
}

auto consume_result_lvalue(Result<std::unique_ptr<int>, int>& source) -> Result<int, int> {
    auto value = rstd_try(source);
    return Ok(*value);
}

auto consume_drop_probe(Result<DropProbe, int>& source) -> Result<int, int> {
    auto value = rstd_try(source);
    return Ok(value.drops != nullptr ? 1 : 0);
}

auto propagate_move_only_error(Result<int, std::unique_ptr<int>>& source)
    -> Result<long, std::unique_ptr<int>> {
    auto value = rstd_try(source);
    return Ok(static_cast<long>(value));
}

auto transform_move_only_error(Result<int, std::unique_ptr<int>>& source)
    -> Result<long, std::unique_ptr<int>> {
    auto value = rstd_try(source, [](std::unique_ptr<int> error) {
        ++*error;
        return error;
    });
    return Ok(static_cast<long>(value));
}

auto propagate_error_reference(Result<int, int&>& source) -> Result<long, int&> {
    auto value = rstd_try(source);
    return Ok(static_cast<long>(value));
}

auto propagate_reference(Result<int&, int>& source) -> Result<int&, int> {
    auto& value = rstd_try(source);
    return Ok<int&>(value);
}

auto propagate_option(bool success, int& continued) -> Option<long> {
    auto value = rstd_try(success ? Some(8) : Option<int> {});
    ++continued;
    return Some(static_cast<long>(value + 1));
}

auto consume_option_lvalue(Option<std::unique_ptr<int>>& source) -> Option<int> {
    auto value = rstd_try(source);
    return Some(*value);
}

auto propagate_option_reference(Option<int&>& source) -> Option<int&> {
    auto& value = rstd_try(source);
    return Some<int&>(value);
}

auto propagate_option_rvalue_reference(Option<int&&>& source) -> Option<int&&> {
    auto&& value = rstd_try(source);
    return Some<int&&>(rstd::move(value));
}

auto option_to_result(bool success, int& fallback_calls) -> Result<int, std::string> {
    auto value = rstd_try(success ? Some(9) : Option<int> {}, [&] {
        ++fallback_calls;
        return std::string { "missing" };
    });
    return Ok(value);
}

auto option_to_prebuilt_result() -> Result<int, std::string> {
    auto value = rstd_try(Option<int> {}, Err(std::string { "missing" }));
    return Ok(value);
}

auto result_to_option(bool success) -> Option<int> {
    auto source = success ? Result<int, int> { Ok(3) } : Result<int, int> { Err(4) };
    auto value  = rstd_try(source, None());
    return Some(value);
}

auto transform_error(bool success, int& fallback_calls) -> Result<int, std::string> {
    auto source   = success ? Result<int, int> { Ok(2) } : Result<int, int> { Err(6) };
    auto fallback = [&](int error) {
        ++fallback_calls;
        return std::to_string(error + 1);
    };
    auto value = rstd_try(source, fallback);
    return Ok(value);
}

auto direct_fallback(int& fallback_calls, bool success) -> Result<int, int> {
    auto source = success ? Result<int, int> { Ok(2) } : Result<int, int> { Err(6) };
    auto value  = rstd_try(source, (++fallback_calls, 10));
    return Ok(value);
}

auto callable_error_fallback(int& fallback_calls) -> Result<int, int> {
    auto source = Result<int, int> { Err(6) };
    auto value  = rstd_try(source, CallableError { &fallback_calls });
    return Ok(value);
}

auto transform_error_with_magic_name() -> Result<int, int> {
    auto value = rstd_try((Result<int, int> { Err(6) }), _e + 3);
    return Ok(value);
}

auto preserve_explicit_residual() -> Result<int, std::string> {
    auto value = rstd_try((Result<int, int> { Err(6) }), Err(std::string { "mapped" }));
    return Ok(value);
}

auto make_nested(bool outer_success, bool inner_success) -> Result<Result<int, int>, int> {
    if (! outer_success) {
        return Err(1);
    }
    if (! inner_success) {
        return Ok(Result<int, int> { Err(2) });
    }
    return Ok(Result<int, int> { Ok(11) });
}

auto propagate_nested(bool outer_success, bool inner_success) -> Result<int, int> {
    auto value = rstd_try(rstd_try(make_nested(outer_success, inner_success)));
    return Ok(value + 1);
}

auto consume_argument(int value) -> int {
    return value + 4;
}

auto use_in_function_argument() -> Result<int, int> {
    return Ok(consume_argument(rstd_try((Result<int, int> { Ok(5) }))));
}

TEST(Try, ResultEvaluatesOnceAndContinuesOnSuccess) {
    int calls     = 0;
    int continued = 0;

    auto result = propagate_result(true, calls, continued);

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.unwrap(), 8);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(continued, 1);
}

TEST(Try, ResultReturnsEarlyOnFailure) {
    int calls     = 0;
    int continued = 0;

    auto result = propagate_result(false, calls, continued);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err(), 5);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(continued, 0);
}

TEST(Try, ResultConvertsErrorThroughPublicConstructor) {
    auto result = convert_error();

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().value, 14);
}

TEST(Try, ResultConsumesMoveOnlyLvalue) {
    auto source = Result<std::unique_ptr<int>, int> { Ok(std::make_unique<int>(21)) };

    auto result = consume_result_lvalue(source);

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.unwrap(), 21);
    ASSERT_TRUE(source.is_ok());
    EXPECT_EQ((*source).get(), nullptr);
}

TEST(Try, SuccessCarrierDropsOwnedValueOnce) {
    int drops = 0;
    {
        auto source = Result<DropProbe, int> { Ok(DropProbe { drops }) };
        auto result = consume_drop_probe(source);

        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.unwrap(), 1);
    }
    EXPECT_EQ(drops, 1);
}

TEST(Try, ResultPropagatesMoveOnlyError) {
    auto source = Result<int, std::unique_ptr<int>> { Err(std::make_unique<int>(31)) };

    auto result = propagate_move_only_error(source);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(*result.unwrap_err(), 31);
}

TEST(Try, CallableFallbackMovesError) {
    auto source = Result<int, std::unique_ptr<int>> { Err(std::make_unique<int>(31)) };

    auto result = transform_move_only_error(source);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(*result.unwrap_err(), 32);
}

TEST(Try, ResultPreservesErrorReference) {
    int  error  = 14;
    auto source = Result<int, int&> { Err<int&>(error) };

    auto result         = propagate_error_reference(source);
    result.unwrap_err() = 19;

    EXPECT_EQ(error, 19);
}

TEST(Try, ResultPreservesReferencePayload) {
    int  value  = 4;
    auto source = Result<int&, int> { Ok<int&>(value) };

    auto result     = propagate_reference(source);
    result.unwrap() = 17;

    EXPECT_EQ(value, 17);
}

TEST(Try, OptionPropagatesNoneWithoutContinuing) {
    int continued = 0;

    auto success = propagate_option(true, continued);
    ASSERT_TRUE(success.is_some());
    EXPECT_EQ(success.unwrap(), 9);
    EXPECT_EQ(continued, 1);

    auto failure = propagate_option(false, continued);
    EXPECT_TRUE(failure.is_none());
    EXPECT_EQ(continued, 1);
}

TEST(Try, OptionConsumesMoveOnlyLvalue) {
    auto source = Some(std::make_unique<int>(22));

    auto result = consume_option_lvalue(source);

    ASSERT_TRUE(result.is_some());
    EXPECT_EQ(result.unwrap(), 22);
    ASSERT_TRUE(source.is_some());
    EXPECT_EQ((*source).get(), nullptr);
}

TEST(Try, OptionPreservesReferencePayload) {
    int  value  = 3;
    auto source = Some<int&>(value);

    auto result     = propagate_option_reference(source);
    result.unwrap() = 18;

    EXPECT_EQ(value, 18);
}

TEST(Try, OptionPreservesRvalueReferencePayload) {
    int  value  = 5;
    auto source = Some<int&&>(rstd::move(value));

    auto   result    = propagate_option_rvalue_reference(source);
    auto&& unwrapped = result.unwrap();
    unwrapped        = 23;

    EXPECT_EQ(value, 23);
}

TEST(Try, OptionMapsNoneToResultExplicitly) {
    int fallback_calls = 0;

    auto success = option_to_result(true, fallback_calls);
    ASSERT_TRUE(success.is_ok());
    EXPECT_EQ(success.unwrap(), 9);
    EXPECT_EQ(fallback_calls, 0);

    auto failure = option_to_result(false, fallback_calls);
    ASSERT_TRUE(failure.is_err());
    EXPECT_EQ(failure.unwrap_err(), "missing");
    EXPECT_EQ(fallback_calls, 1);
}

TEST(Try, OptionPreservesPrebuiltResidual) {
    auto result = option_to_prebuilt_result();

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err(), "missing");
}

TEST(Try, ResultMapsFailureToOptionExplicitly) {
    EXPECT_TRUE(result_to_option(true).is_some());
    EXPECT_TRUE(result_to_option(false).is_none());
}

TEST(Try, CallableFallbackRunsOnlyOnFailure) {
    int fallback_calls = 0;

    auto success = transform_error(true, fallback_calls);
    ASSERT_TRUE(success.is_ok());
    EXPECT_EQ(success.unwrap(), 2);
    EXPECT_EQ(fallback_calls, 0);

    auto failure = transform_error(false, fallback_calls);
    ASSERT_TRUE(failure.is_err());
    EXPECT_EQ(failure.unwrap_err(), "7");
    EXPECT_EQ(fallback_calls, 1);
}

TEST(Try, DirectFallbackExpressionRunsOnlyOnFailure) {
    int fallback_calls = 0;

    auto success = direct_fallback(fallback_calls, true);
    ASSERT_TRUE(success.is_ok());
    EXPECT_EQ(fallback_calls, 0);

    auto failure = direct_fallback(fallback_calls, false);
    ASSERT_TRUE(failure.is_err());
    EXPECT_EQ(failure.unwrap_err(), 10);
    EXPECT_EQ(fallback_calls, 1);
}

TEST(Try, CallableErrorObjectUsesCallablePath) {
    int fallback_calls = 0;

    auto result = callable_error_fallback(fallback_calls);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err(), 26);
    EXPECT_EQ(fallback_calls, 1);
}

TEST(Try, DirectFallbackCanUseErrorName) {
    auto result = transform_error_with_magic_name();

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err(), 9);
}

TEST(Try, ExplicitResidualIsNotWrappedAgain) {
    auto result = preserve_explicit_residual();

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err(), "mapped");
}

TEST(Try, NestedUsesHaveIndependentScopes) {
    auto success = propagate_nested(true, true);
    ASSERT_TRUE(success.is_ok());
    EXPECT_EQ(success.unwrap(), 12);

    auto inner_failure = propagate_nested(true, false);
    ASSERT_TRUE(inner_failure.is_err());
    EXPECT_EQ(inner_failure.unwrap_err(), 2);

    auto outer_failure = propagate_nested(false, true);
    ASSERT_TRUE(outer_failure.is_err());
    EXPECT_EQ(outer_failure.unwrap_err(), 1);
}

TEST(Try, CanBeUsedAsFunctionArgument) {
    auto result = use_in_function_argument();

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.unwrap(), 9);
}

TEST(Try, WorksFromModuleGlobalFragment) {
    auto success = try_module_check(true);
    ASSERT_TRUE(success.is_ok());
    EXPECT_EQ(success.unwrap(), 7);

    auto failure = try_module_check(false);
    ASSERT_TRUE(failure.is_err());
    EXPECT_EQ(failure.unwrap_err(), 7);
}

} // namespace

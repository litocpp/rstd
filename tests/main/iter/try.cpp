#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace iter = rstd::iter;

static_assert(
    rstd::mtp::same_as<rstd::try_::change_output_t<Result<i32, i32&>, bool>, Result<bool, i32&>>);
static_assert(rstd::mtp::same_as<rstd::try_::change_output_t<Option<i32>, bool>, Option<bool>>);
static_assert(
    rstd::mtp::same_as<rstd::try_::change_output_t<rstd::ops::ControlFlow<i32&, i32>, bool>,
                       rstd::ops::ControlFlow<i32&, bool>>);

constexpr auto try_queries() -> bool {
    auto source = iter::range(1_i32, 6_i32);
    auto found  = source.try_find([](const i32& x) -> Result<bool, i32> {
        if (x == 3_i32) return Err(3_i32);
        return Ok(false);
    });
    if (! found.is_err() || found.unwrap_err() != 3_i32 || source.next().unwrap() != 4_i32)
        return false;
    auto sum = source.try_reduce([](i32 a, i32 b) -> Result<i32, i32> {
        return Ok(a + b);
    });
    if (! sum.is_ok() || sum->unwrap() != 5_i32) return false;
    auto empty = iter::empty<i32>().try_reduce([](i32 a, i32 b) {
        return Some(a + b);
    });
    return empty.is_some() && empty->is_none();
}
static_assert(try_queries());

TEST(IterTry, FindAndReduceConstexpr) {
    EXPECT_TRUE(try_queries());
}

TEST(IterTry, FindMatchAndMissing) {
    auto source = iter::range(1_i32, 5_i32);
    auto found  = source.try_find([](const i32& x) {
        return Some(x == 2_i32);
    });
    ASSERT_TRUE(found.is_some());
    EXPECT_EQ(found->unwrap(), 2_i32);
    EXPECT_EQ(source.next().unwrap(), 3_i32);
    auto missing = source.try_find([](const i32&) {
        return Some(false);
    });
    EXPECT_TRUE(missing->is_none());
    auto failed = iter::once(1_i32).try_find([](const i32&) -> Option<bool> {
        return None();
    });
    EXPECT_TRUE(failed.is_none());
}

TEST(IterTry, ReduceFailureLeavesRemainder) {
    auto source = iter::range(1_i32, 7_i32);
    int  calls  = 0;
    auto result = source.try_reduce([&](i32 a, i32 b) -> Result<i32, i32> {
        ++calls;
        if (b == 3_i32) return Err(b);
        return Ok(a + b);
    });
    EXPECT_EQ(result.unwrap_err(), 3_i32);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(source.next().unwrap(), 4_i32);
}

TEST(IterTry, CollectControlFlowAndResume) {
    using Flow  = rstd::ops::ControlFlow<i32, i32>;
    auto source = iter::range(1_i32, 6_i32).map([](i32 x) {
        return x == 3_i32 ? Flow::Break(x) : Flow::Continue(x);
    });
    auto failed = source.try_collect<Vec<i32>>();
    ASSERT_TRUE(failed.is_break());
    EXPECT_EQ(failed.break_value_unchecked(), 3_i32);
    auto rest = source.try_collect<Vec<i32>>();
    ASSERT_TRUE(rest.is_continue());
    const auto& values = rest.continue_value_unchecked();
    ASSERT_EQ(values.len(), 2_usize);
    EXPECT_EQ(values[0_usize], 4_i32);
    EXPECT_EQ(values[1_usize], 5_i32);
}

TEST(IterTry, CollectIntoPreservesOwner) {
    auto  values = iter::once(9_i32).collect<Vec<i32>>();
    auto& same   = iter::range(1_i32, 4_i32).collect_into(values);
    EXPECT_EQ(&same, &values);
    ASSERT_EQ(values.len(), 4_usize);
    EXPECT_EQ(values[0_usize], 9_i32);
    EXPECT_EQ(values[3_usize], 3_i32);
}

TEST(IterTry, PartitionStopsAtFirstViolation) {
    auto source = iter::range(0_i32, 6_i32);
    EXPECT_FALSE(source.by_ref().is_partitioned([](i32 x) {
        return x % 2_i32 == 0_i32;
    }));
    EXPECT_EQ(source.next().unwrap(), 3_i32);
    EXPECT_TRUE(iter::range(0_i32, 6_i32).is_partitioned([](i32 x) {
        return x < 3_i32;
    }));
    EXPECT_TRUE(iter::empty<i32>().is_partitioned([](i32) {
        return false;
    }));
}

struct ReduceItem {
    int  value;
    int* alive;
    ReduceItem(int n, int& count): value(n), alive(&count) { ++*alive; }
    ReduceItem(ReduceItem&& other)
        : value(other.value), alive(rstd::exchange(other.alive, nullptr)) {}
    ReduceItem(const ReduceItem&)               = delete;
    auto operator=(ReduceItem&&) -> ReduceItem& = delete;
    ~ReduceItem() {
        if (alive) --*alive;
    }
};

TEST(IterTry, NonAssignableReduceAndDrop) {
    int alive = 0;
    {
        auto source = iter::range(1_i32, 4_i32).map([&](i32 n) {
            return ReduceItem(n.to_primitive(), alive);
        });
        auto result = source.try_reduce([&](ReduceItem a, ReduceItem b) -> Result<ReduceItem, i32> {
            return Ok(ReduceItem(a.value + b.value, alive));
        });
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ((**result).value, 6);
        EXPECT_EQ(alive, 1);
    }
    EXPECT_EQ(alive, 0);
}

TEST(IterTry, ReferenceIdentityAndError) {
    i32  values[] = { 1_i32, 2_i32 };
    i32  error    = 7_i32;
    auto source   = iter::from_array(values);
    auto found    = source.try_find([](const auto& item) -> Result<bool, i32> {
        return Ok(*item == 2_i32);
    });
    EXPECT_EQ(found->unwrap().as_raw_ptr(), &values[1]);
    auto failed = iter::once(1_i32).try_find([&](const i32&) -> Result<bool, i32&> {
        return Err<i32&>(error);
    });
    EXPECT_EQ(&failed.unwrap_err(), &error);
}

TEST(IterTry, CollectOptionAndResult) {
    auto optional = iter::range(1_i32, 4_i32)
                        .map([](i32 x) {
                            return Some(x);
                        })
                        .try_collect<Vec<i32>>();
    EXPECT_EQ(optional->len(), 3_usize);
    auto result = iter::range(1_i32, 4_i32)
                      .map([](i32 x) -> Result<i32, i32> {
                          return Ok(x);
                      })
                      .try_collect<Vec<i32>>();
    EXPECT_EQ(result->len(), 3_usize);
}

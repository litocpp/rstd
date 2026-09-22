#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace iter = rstd::iter;

struct GroupConstantHasher {
    void write(rstd::slice<rstd::u8>) noexcept {}
    auto finish() const noexcept -> u64 { return 7_u64; }
};
using GroupHasher = rstd::hash::BuildHasherDefault<GroupConstantHasher>;

TEST(IterGroup, FirstKeyAndMemberOrderWithCollisions) {
    auto groups = iter::group_by(
        iter::range(1_i32, 6_i32),
        [](const i32& x) {
            return x % 2_i32;
        },
        GroupHasher());
    ASSERT_EQ(groups.len(), 2_usize);
    EXPECT_EQ(groups[0_usize].key, 1_i32);
    EXPECT_EQ(groups[1_usize].key, 0_i32);
    ASSERT_EQ(groups[0_usize].items.len(), 3_usize);
    EXPECT_EQ(groups[0_usize].items[2_usize], 5_i32);
    EXPECT_EQ(groups[1_usize].items[1_usize], 4_i32);
}

TEST(IterGroup, CountsAndTotals) {
    auto parity = [](const i32& x) {
        return x % 2_i32;
    };
    auto counts = iter::count_by(iter::range(1_i32, 6_i32), parity);
    ASSERT_TRUE(counts.is_ok());
    EXPECT_EQ((*counts)[0_usize].template get<0>(), 1_i32);
    EXPECT_EQ((*counts)[0_usize].template get<1>(), 3_usize);
    int  initializations = 0;
    auto totals          = iter::fold_by(
        iter::range(1_i32, 6_i32),
        parity,
        [&](const i32&) {
            ++initializations;
            return 0_i32;
        },
        [](i32 sum, i32 x) {
            return sum + x;
        });
    EXPECT_EQ(initializations, 2);
    EXPECT_EQ(totals[0_usize].template get<1>(), 9_i32);
    EXPECT_EQ(totals[1_usize].template get<1>(), 6_i32);
    usize full = usize::MAX;
    EXPECT_TRUE(iter::increment_count(full).is_err());
    EXPECT_EQ(full, usize::MAX);
}

TEST(IterGroup, EmptyAndBorrowedStorage) {
    auto empty = iter::group_by(iter::empty<i32>(), [](const i32& x) {
        return x;
    });
    EXPECT_TRUE(empty.is_empty());
    int  source[] = { 1, 2, 1 };
    auto groups   = iter::group_by(iter::from_array(source), [](auto item) {
        return i32(*item);
    });
    EXPECT_EQ(groups[0_usize].items[1_usize].as_raw_ptr(), &source[2]);
    auto index = iter::build_group_index(iter::range(1_i32, 6_i32), [](const i32& x) {
        return x % 2_i32;
    });
    EXPECT_EQ(index.get(1_i32).unwrap()->len(), 3_usize);
    EXPECT_TRUE(index.get(7_i32).is_none());
    auto moved = rstd::move(index).into_groups();
    EXPECT_EQ(moved[1_usize].items.len(), 2_usize);
}

struct GroupAccumulator {
    i32 sum;
    explicit GroupAccumulator(i32 sum): sum(sum) {}
    GroupAccumulator(GroupAccumulator&&)                    = default;
    GroupAccumulator(const GroupAccumulator&)               = delete;
    auto operator=(GroupAccumulator&&) -> GroupAccumulator& = delete;
};

TEST(IterGroup, NonAssignableAccumulator) {
    auto values = iter::fold_by(
        iter::range(1_i32, 6_i32),
        [](const i32& x) {
            return x % 2_i32;
        },
        [](const i32&) {
            return GroupAccumulator(0_i32);
        },
        [](GroupAccumulator sum, i32 x) {
            return GroupAccumulator(sum.sum + x);
        });
    EXPECT_EQ(values[0_usize].template get<1>().sum, 9_i32);
    EXPECT_EQ(values[1_usize].template get<1>().sum, 6_i32);
}

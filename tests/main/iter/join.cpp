#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::tuple;
namespace iter = rstd::iter;

TEST(IterJoin, RepeatedKeysProduceAllPairs) {
    auto parity = [](const i32& x) {
        return x % 2_i32;
    };
    auto values = iter::join_by(iter::range(1_i32, 4_i32),
                                iter::range(1_i32, 5_i32),
                                parity,
                                parity,
                                [](const i32& a, const i32& b) {
                                    return tuple<i32, i32>(a, b);
                                })
                      .collect<Vec<tuple<i32, i32>>>();
    ASSERT_EQ(values.len(), 6_usize);
    const int left[]  = { 1, 1, 2, 2, 3, 3 };
    const int right[] = { 1, 3, 2, 4, 1, 3 };
    for (usize i; i < values.len(); ++i) {
        EXPECT_EQ(values[i].template get<0>(), i32(left[i.to_primitive()]));
        EXPECT_EQ(values[i].template get<1>(), i32(right[i.to_primitive()]));
    }
}

TEST(IterJoin, EmptyOuterDoesNotIndexInner) {
    int  inner_reads = 0;
    auto key         = [](const i32& x) {
        return x;
    };
    auto joined = iter::join_by(iter::empty<i32>(),
                                iter::from_fn([&] {
                                    ++inner_reads;
                                    return Some(1_i32);
                                }),
                                key,
                                key,
                                [](const i32& a, const i32& b) {
                                    return a + b;
                                });
    EXPECT_EQ(inner_reads, 0);
    EXPECT_TRUE(joined.next().is_none());
    EXPECT_EQ(inner_reads, 0);
}

TEST(IterJoin, GroupJoinIncludesEmptyGroups) {
    auto key = [](const i32& x) {
        return x;
    };
    auto groups = iter::group_join_by(iter::range(1_i32, 4_i32),
                                      iter::range(1_i32, 3_i32),
                                      key,
                                      key,
                                      [](const i32& outer, slice<i32> matches) {
                                          return tuple<i32, usize>(outer, matches.len());
                                      })
                      .collect<Vec<tuple<i32, usize>>>();
    ASSERT_EQ(groups.len(), 3_usize);
    EXPECT_EQ(groups[0_usize].template get<1>(), 1_usize);
    EXPECT_EQ(groups[2_usize].template get<1>(), 0_usize);
}

struct JoinOwned {
    i32  value;
    int* alive;
    JoinOwned(i32 x, int& count): value(x), alive(&count) { ++*alive; }
    JoinOwned(JoinOwned&& other): value(other.value), alive(rstd::exchange(other.alive, nullptr)) {}
    JoinOwned(const JoinOwned&)               = delete;
    auto operator=(JoinOwned&&) -> JoinOwned& = delete;
    ~JoinOwned() {
        if (alive) --*alive;
    }
};

TEST(IterJoin, EarlyDropReleasesMoveOnlyIndex) {
    int  alive = 0;
    auto key   = [](const i32& x) {
        return x;
    };
    {
        auto inner  = iter::range(1_i32, 4_i32).map([&](i32 x) {
            return JoinOwned(x, alive);
        });
        auto joined = iter::join_by(
            iter::repeat(1_i32),
            rstd::move(inner),
            key,
            [](const JoinOwned& x) {
                return x.value;
            },
            [](const i32& outer, const JoinOwned& inner) {
                return outer + inner.value;
            });
        EXPECT_EQ(alive, 0);
        EXPECT_EQ(joined.next().unwrap(), 2_i32);
        EXPECT_EQ(alive, 3);
    }
    EXPECT_EQ(alive, 0);
}

TEST(IterJoin, MovingAdapterKeepsIndexViewsValid) {
    auto key = [](const i32&) {
        return 1_i32;
    };
    auto joined = iter::join_by(iter::range(1_i32, 3_i32),
                                iter::range(1_i32, 4_i32),
                                key,
                                key,
                                [](const i32& a, const i32& b) {
                                    return a * 10_i32 + b;
                                });
    EXPECT_EQ(joined.next().unwrap(), 11_i32);
    auto moved = rstd::move(joined);
    EXPECT_EQ(moved.next().unwrap(), 12_i32);
    auto rest = rstd::move(moved).collect<Vec<i32>>();
    ASSERT_EQ(rest.len(), 4_usize);
    EXPECT_EQ(rest[0_usize], 13_i32);
    EXPECT_EQ(rest[3_usize], 23_i32);
}

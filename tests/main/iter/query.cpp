#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace iter = rstd::iter;

TEST(IterQuery, SingleAndRemainder) {
    EXPECT_EQ(iter::empty<i32>().single().unwrap_err(), iter::SingleError::Empty);
    EXPECT_EQ(iter::once(1_i32).single().unwrap(), 1_i32);
    auto source = iter::range(1_i32, 5_i32);
    EXPECT_EQ(source.by_ref().single().unwrap_err(), iter::SingleError::Multiple);
    EXPECT_EQ(source.next().unwrap(), 3_i32);
}

TEST(IterQuery, DefaultIfEmpty) {
    auto empty = iter::empty<i32>().default_if_empty(9_i32);
    EXPECT_EQ(empty.next().unwrap(), 9_i32);
    EXPECT_TRUE(empty.next().is_none());
    auto values = iter::range(1_i32, 3_i32).default_if_empty(9_i32).collect<Vec<i32>>();
    ASSERT_EQ(values.len(), 2_usize);
    EXPECT_EQ(values[0_usize], 1_i32);
    EXPECT_EQ(values[1_usize], 2_i32);
}

TEST(IterQuery, ChunksAreLazyAndKeepTail) {
    int  reads  = 0;
    auto blocks = iter::chunks(iter::range(1_i32, 6_i32).inspect([&](i32) {
        ++reads;
    }),
                               2_usize);
    EXPECT_EQ(reads, 0);
    EXPECT_EQ(blocks.size_hint().template get<0>(), 3_usize);
    auto first = blocks.next().unwrap();
    EXPECT_EQ(reads, 2);
    EXPECT_EQ(first[1_usize], 2_i32);
    EXPECT_EQ(blocks.next()->len(), 2_usize);
    EXPECT_EQ(blocks.next()->len(), 1_usize);
    EXPECT_TRUE(blocks.next().is_none());
    EXPECT_EQ(reads, 5);
    EXPECT_EQ(first[0_usize], 1_i32);
}

TEST(IterQuery, HugeChunksAndExplicitBorrow) {
    auto short_block = iter::chunks(iter::once(1_i32), usize::MAX).next().unwrap();
    EXPECT_EQ(short_block.len(), 1_usize);
    EXPECT_LT(short_block.capacity(), 100_usize);
    int  values[] = { 1, 2 };
    auto borrowed = iter::chunks(iter::from_array_mut(values), 2_usize).next().unwrap();
    EXPECT_EQ(borrowed[0_usize].as_raw_ptr(), &values[0]);
    *borrowed[1_usize] = 7;
    EXPECT_EQ(values[1], 7);
}

TEST(IterQuery, UniquePreservesFirstOccurrence) {
    int  calls  = 0;
    auto source = iter::unique_by(iter::range(1_i32, 6_i32), [&](const i32& x) {
        ++calls;
        return x % 2_i32;
    });
    EXPECT_EQ(calls, 0);
    auto values = rstd::move(source).collect<Vec<i32>>();
    EXPECT_EQ(calls, 5);
    ASSERT_EQ(values.len(), 2_usize);
    EXPECT_EQ(values[0_usize], 1_i32);
    EXPECT_EQ(values[1_usize], 2_i32);
    auto cloned =
        iter::unique(iter::range(1_i32, 4_i32).chain(iter::once(2_i32))).collect<Vec<i32>>();
    EXPECT_EQ(cloned.len(), 3_usize);
}

struct QueryOwned {
    i32  key;
    int* alive;
    QueryOwned(i32 value, int& count): key(value), alive(&count) { ++*alive; }
    QueryOwned(QueryOwned&& other): key(other.key), alive(rstd::exchange(other.alive, nullptr)) {}
    QueryOwned(const QueryOwned&)               = delete;
    auto operator=(QueryOwned&&) -> QueryOwned& = delete;
    ~QueryOwned() {
        if (alive) --*alive;
    }
};

TEST(IterQuery, UniqueMoveOnlyEarlyDrop) {
    int alive = 0;
    {
        auto values = iter::unique_by(iter::range(0_i32, 20_i32).map([&](i32 x) {
                          return QueryOwned(x, alive);
                      }),
                                      [](const QueryOwned& x) {
                                          return x.key % 3_i32;
                                      })
                          .take(2_usize)
                          .collect<Vec<QueryOwned>>();
        EXPECT_EQ(alive, 2);
    }
    EXPECT_EQ(alive, 0);
}

TEST(IterQuery, NonFusedSourcesEndPermanently) {
    int  reads  = 0;
    auto source = iter::from_fn([&]() -> Option<i32> {
        ++reads;
        if (reads == 2) return None();
        return Some(1_i32);
    });
    auto chunks = iter::chunks(rstd::move(source), 3_usize);
    EXPECT_EQ(chunks.next()->len(), 1_usize);
    EXPECT_TRUE(chunks.next().is_none());
    EXPECT_EQ(reads, 2);
}

TEST(IterQuery, TailOrderAndReadCounts) {
    int  reads   = 0;
    auto delayed = iter::skip_last(iter::range(1_i32, 8_i32).inspect([&](i32) {
        ++reads;
    }),
                                   2_usize);
    EXPECT_EQ(reads, 0);
    EXPECT_EQ(delayed.next().unwrap(), 1_i32);
    EXPECT_EQ(reads, 3);
    auto rest = rstd::move(delayed).collect<Vec<i32>>();
    EXPECT_EQ(rest.len(), 4_usize);
    EXPECT_EQ(rest[3_usize], 5_i32);
    reads     = 0;
    auto last = iter::take_last(iter::range(1_i32, 8_i32).inspect([&](i32) {
        ++reads;
    }),
                                2_usize);
    EXPECT_EQ(reads, 0);
    EXPECT_EQ(last.next().unwrap(), 6_i32);
    EXPECT_EQ(reads, 7);
    EXPECT_EQ(last.next().unwrap(), 7_i32);
    EXPECT_TRUE(last.next().is_none());
    reads = 0;
    EXPECT_TRUE(iter::take_last(iter::from_fn([&] {
                                    ++reads;
                                    return Some(1_i32);
                                }),
                                0_usize)
                    .next()
                    .is_none());
    EXPECT_EQ(reads, 0);
    auto infinite = iter::skip_last(iter::repeat(1_i32), 3_usize).take(5_usize).collect<Vec<i32>>();
    EXPECT_EQ(infinite.len(), 5_usize);
}

TEST(IterQuery, SetOrderAndDuplicateSuppression) {
    auto parity = [](const i32& x) {
        return x % 3_i32;
    };
    auto common =
        iter::intersection_by(iter::range(1_i32, 8_i32), iter::range(2_i32, 4_i32), parity, parity)
            .collect<Vec<i32>>();
    ASSERT_EQ(common.len(), 2_usize);
    EXPECT_EQ(common[0_usize], 2_i32);
    EXPECT_EQ(common[1_usize], 3_i32);
    auto difference =
        iter::difference_by(iter::range(1_i32, 8_i32), iter::range(2_i32, 4_i32), parity, parity)
            .collect<Vec<i32>>();
    ASSERT_EQ(difference.len(), 1_usize);
    EXPECT_EQ(difference[0_usize], 1_i32);
    auto combined =
        iter::union_by(iter::range(1_i32, 3_i32), iter::range(2_i32, 6_i32), parity, parity)
            .collect<Vec<i32>>();
    ASSERT_EQ(combined.len(), 3_usize);
    EXPECT_EQ(combined[0_usize], 1_i32);
    EXPECT_EQ(combined[1_usize], 2_i32);
    EXPECT_EQ(combined[2_usize], 3_i32);
}

TEST(IterQuery, EmptyLeftSkipsRightIndex) {
    int  reads = 0;
    auto key   = [](const i32& x) {
        return x;
    };
    auto source = iter::intersection_by(iter::empty<i32>(),
                                        iter::from_fn([&] {
                                            ++reads;
                                            return Some(1_i32);
                                        }),
                                        key,
                                        key);
    EXPECT_TRUE(source.next().is_none());
    EXPECT_EQ(reads, 0);
}

struct QueryConstantHasher {
    void write(rstd::slice<rstd::u8>) noexcept {}
    auto finish() const noexcept -> u64 { return 7_u64; }
};
using QueryHasher = rstd::hash::BuildHasherDefault<QueryConstantHasher>;

TEST(IterQuery, StatefulEqualityStrategy) {
    struct Equal {
        i32  modulus;
        bool operator()(i32 a, i32 b) const { return a % modulus == b % modulus; }
    };
    auto values = iter::unique_by(
                      iter::range(1_i32, 8_i32),
                      [](const i32& x) {
                          return x;
                      },
                      QueryHasher(),
                      Equal { 3_i32 })
                      .collect<Vec<i32>>();
    ASSERT_EQ(values.len(), 3_usize);
    EXPECT_EQ(values[2_usize], 3_i32);
}

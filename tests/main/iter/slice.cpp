#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace iter   = rstd::iter;
namespace slices = rstd::slice_;

constexpr auto slice_chunks() -> bool {
    const int data[] = { 1, 2, 3, 4, 5 };
    auto      source = slice<int>::from_raw_parts(data, 5_usize);
    auto      chunks = slices::chunks(source, 2_usize);
    if (chunks.len() != 3_usize) return false;
    auto tail = chunks.next_back().unwrap();
    if (tail.len() != 1_usize || tail.as_raw_ptr() != &data[4]) return false;
    if (chunks.next()->as_raw_ptr() != data) return false;
    if (chunks.next_back()->as_raw_ptr() != &data[2]) return false;
    if (chunks.next().is_some() || chunks.len() != 0_usize) return false;
    auto exact = slices::chunks_exact(source, 2_usize);
    if (exact.remainder().as_raw_ptr() != &data[4] || exact.remainder().len() != 1_usize)
        return false;
    if (exact.next_back()->len() != 2_usize || exact.next()->len() != 2_usize) return false;
    return exact.next().is_none() && exact.remainder()[0_usize] == 5;
}

constexpr auto slice_windows() -> bool {
    const int data[]  = { 1, 2, 3, 4 };
    auto      windows = slices::windows(slice<int>::from_raw_parts(data, 4_usize), 2_usize);
    if (windows.len() != 3_usize || windows.next()->as_raw_ptr() != data) return false;
    if (windows.next_back()->as_raw_ptr() != &data[2]) return false;
    if (windows.next()->as_raw_ptr() != &data[1]) return false;
    return windows.next_back().is_none();
}

constexpr auto contiguous_groups() -> bool {
    const int data[] = { 1, 1, 2, 1 };
    auto groups = slices::chunk_by(slice<int>::from_raw_parts(data, 4_usize), [](int a, int b) {
        return a == b;
    });
    if (groups.next()->len() != 2_usize) return false;
    if (groups.next_back()->as_raw_ptr() != &data[3]) return false;
    if (groups.next()->as_raw_ptr() != &data[2]) return false;
    return groups.next_back().is_none();
}

static_assert(slice_chunks());
static_assert(slice_windows());
static_assert(contiguous_groups());

TEST(IterSlice, BorrowedChunks) {
    EXPECT_TRUE(slice_chunks());
}
TEST(IterSlice, OverlappingWindows) {
    EXPECT_TRUE(slice_windows());
}
TEST(IterSlice, AdjacentGroups) {
    EXPECT_TRUE(contiguous_groups());
}

TEST(IterSlice, EmptyAndOversizedWidths) {
    slice<int> empty;
    EXPECT_TRUE(slices::chunks(empty, usize::MAX).next().is_none());
    EXPECT_TRUE(slices::windows(empty, usize::MAX).next().is_none());
    const int data[] = { 1 };
    auto      source = slice<int>::from_raw_parts(data, 1_usize);
    EXPECT_EQ(slices::chunks(source, usize::MAX).next()->len(), 1_usize);
    auto exact = slices::chunks_exact(source, usize::MAX);
    EXPECT_TRUE(exact.next().is_none());
    EXPECT_EQ(exact.remainder().len(), 1_usize);
    EXPECT_TRUE(slices::windows(source, usize::MAX).next().is_none());
}

TEST(IterSlice, ZeroWidthRejected) {
    EXPECT_DEATH((void)slices::chunks(slice<int>(), 0_usize), "width 0");
    EXPECT_DEATH((void)slices::chunks_exact(slice<int>(), 0_usize), "width 0");
    EXPECT_DEATH((void)slices::windows(slice<int>(), 0_usize), "width 0");
}

TEST(IterSlice, ReversePredicateKeepsArgumentOrder) {
    const int data[] = { 1, 2, 3, 0, 1 };
    auto groups = slices::chunk_by(slice<int>::from_raw_parts(data, 5_usize), [](int a, int b) {
        return a < b;
    });
    EXPECT_EQ(groups.next_back()->len(), 2_usize);
    EXPECT_EQ(groups.next()->len(), 3_usize);
}

TEST(IterFactory, SeparatorsAreLazy) {
    int  calls  = 0;
    auto source = iter::range(1_i32, 4_i32).intersperse_with([&] {
        ++calls;
        return 0_i32;
    });
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(source.next().unwrap(), 1_i32);
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(source.next().unwrap(), 0_i32);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(source.next().unwrap(), 2_i32);
    EXPECT_EQ(calls, 1);
    auto rest = rstd::move(source).collect<Vec<i32>>();
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(rest.len(), 2_usize);
    EXPECT_EQ(rest[1_usize], 3_i32);
    EXPECT_TRUE(iter::empty<i32>()
                    .intersperse_with([&] {
                        ++calls;
                        return 0_i32;
                    })
                    .next()
                    .is_none());
    EXPECT_EQ(calls, 2);
}

struct FactoryItem {
    int value;
    explicit constexpr FactoryItem(int n): value(n) {}
    constexpr FactoryItem(FactoryItem&&)          = default;
    FactoryItem(const FactoryItem&)               = delete;
    auto operator=(FactoryItem&&) -> FactoryItem& = delete;
};

constexpr auto move_only_separators() -> bool {
    auto source = iter::range(1_i32, 3_i32)
                      .map([](i32 x) {
                          return FactoryItem(x.to_primitive());
                      })
                      .intersperse_with([] {
                          return FactoryItem(0);
                      });
    return source.next()->value == 1 && source.next()->value == 0 && source.next()->value == 2 &&
           source.next().is_none();
}
static_assert(move_only_separators());
TEST(IterFactory, NonAssignableItems) {
    EXPECT_TRUE(move_only_separators());
}

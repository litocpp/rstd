#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;

using namespace rstd::prelude;

TEST(AllocationTracker, WindowPreservesExistingLiveBytesAndDeferredDestruction) {
    rstd::alloc::AllocationTracker tracker;
    auto allocator = rstd::alloc::TrackingAllocator(::alloc::Global {}, tracker);
    {
        auto values = Vec<u64, decltype(allocator)>::with_capacity_in(usize(4), allocator);
        auto before = tracker.snapshot();
        EXPECT_EQ(before.live_bytes, u64(4 * sizeof(u64)));
        ASSERT_TRUE(tracker.begin_window().is_ok());
        EXPECT_TRUE(tracker.begin_window().is_err());
        for (u64 i; i < u64(9); ++i) values.push(u64(i));
        auto measured = tracker.end_window();
        ASSERT_TRUE(measured.is_ok());
        EXPECT_EQ(measured->before.live_bytes, before.live_bytes);
        EXPECT_GT(measured->after.live_bytes, before.live_bytes);
        EXPECT_EQ(measured->peak_live_bytes, measured->after.live_bytes);
        EXPECT_FALSE(measured->after.overflow);
        EXPECT_TRUE(tracker.end_window().is_err());
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
    EXPECT_FALSE(tracker.snapshot().overflow);
}

struct RejectGrowAllocator : rstd::DefaultInClass<RejectGrowAllocator, rstd::alloc::Allocator> {
    auto allocate(rstd::alloc::Layout layout) const
        -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return as<rstd::alloc::Allocator>(::alloc::GLOBAL).allocate(layout);
    }
    void deallocate(void* pointer, rstd::alloc::Layout layout) const noexcept {
        as<rstd::alloc::Allocator>(::alloc::GLOBAL).deallocate(pointer, layout);
    }
    auto grow(void*, rstd::alloc::Layout, rstd::alloc::Layout) const
        -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        return Err(rstd::alloc::AllocError {});
    }
};

TEST(AllocationTracker, FailedGrowKeepsOriginalAllocationAlive) {
    rstd::alloc::AllocationTracker tracker;
    auto allocator  = rstd::alloc::TrackingAllocator(RejectGrowAllocator {}, tracker);
    auto old_layout = rstd::alloc::Layout::make<u64>();
    auto new_layout = rstd::alloc::Layout::array<u64>(usize(2)).unwrap();
    auto allocated  = allocator.allocate(old_layout);
    ASSERT_TRUE(allocated.is_ok());
    auto grown = allocator.grow(allocated->pointer, old_layout, new_layout);
    EXPECT_TRUE(grown.is_err());
    EXPECT_EQ(tracker.snapshot().live_bytes, u64(sizeof(u64)));
    EXPECT_EQ(tracker.snapshot().failures, u64(1));
    EXPECT_EQ(tracker.snapshot().grows, u64(1));
    allocator.deallocate(allocated->pointer, old_layout);
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(AllocationTracker, CopiesShareZeroSizedAndResizeAccounting) {
    using rstd::alloc::Layout;
    rstd::alloc::AllocationTracker tracker;
    auto first  = rstd::alloc::TrackingAllocator(::alloc::Global {}, tracker);
    auto second = first;
    auto zero   = Layout::from_size_align(usize(), usize(8)).unwrap();
    auto small  = Layout::make<u64>();
    auto large  = Layout::array<u64>(usize(2)).unwrap();
    auto empty  = first.allocate(zero);
    ASSERT_TRUE(empty.is_ok());
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
    second.deallocate(empty->pointer, zero);
    auto allocated = first.allocate_zeroed(small);
    ASSERT_TRUE(allocated.is_ok());
    auto grown = second.grow_zeroed(allocated->pointer, small, large);
    ASSERT_TRUE(grown.is_ok());
    auto shrunk = first.shrink(grown->pointer, large, small);
    ASSERT_TRUE(shrunk.is_ok());
    second.deallocate(shrunk->pointer, small);
    auto stats = tracker.snapshot();
    EXPECT_EQ(stats.allocations, u64(2));
    EXPECT_EQ(stats.deallocations, u64(2));
    EXPECT_EQ(stats.grows, u64(1));
    EXPECT_EQ(stats.shrinks, u64(1));
    EXPECT_EQ(stats.requested_bytes, u64(4 * sizeof(u64)));
    EXPECT_EQ(stats.peak_live_bytes, u64(2 * sizeof(u64)));
    EXPECT_EQ(stats.live_bytes, u64());
    EXPECT_FALSE(stats.overflow);
}

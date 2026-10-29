#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;
using rstd::alloc::Allocator;
using rstd::alloc::BudgetAllocator;
using rstd::alloc::Layout;
using rstd::alloc::PoolAllocator;
using rstd::alloc::PoolOptions;
using rstd::alloc::SharedPoolAllocator;

template<typename A>
void exercise_pool_in_place(const A& pool) {
    auto api          = as<Allocator>(pool);
    auto full         = Layout::from_size_align(usize(32), usize(16)).unwrap();
    auto small        = Layout::from_size_align(usize(17), usize(1)).unwrap();
    auto grown_layout = Layout::from_size_align(usize(31), usize(16)).unwrap();
    auto tiny         = Layout::from_size_align(usize(7), usize(8)).unwrap();
    auto initial      = api.allocate(full).unwrap();
    rstd::mem::memset(initial.pointer, u8(0xab), full.size);
    auto baseline = pool.statistics();
    auto shrunk   = api.shrink(initial.pointer, full, small).unwrap();
    EXPECT_EQ(shrunk.pointer, initial.pointer);
    EXPECT_EQ(shrunk.size, small.size);
    EXPECT_EQ(pool.statistics().live_bytes, small.size);
    auto grown = api.grow(shrunk.pointer, small, grown_layout).unwrap();
    EXPECT_EQ(grown.pointer, initial.pointer);
    EXPECT_EQ(grown.size, grown_layout.size);
    for (unsigned i = 0; i < 31; ++i)
        EXPECT_EQ(static_cast<unsigned char*>(grown.pointer)[i], 0xab);
    shrunk      = api.shrink(grown.pointer, grown_layout, tiny).unwrap();
    auto zeroed = api.grow_zeroed(shrunk.pointer, tiny, full).unwrap();
    EXPECT_EQ(zeroed.pointer, initial.pointer);
    for (unsigned i = 0; i < 32; ++i)
        EXPECT_EQ(static_cast<unsigned char*>(zeroed.pointer)[i], i < 7 ? 0xab : 0);
    auto stats = pool.statistics();
    EXPECT_EQ(stats.live_bytes, full.size);
    EXPECT_EQ(stats.peak_live_bytes, full.size);
    EXPECT_EQ(stats.live_block_bytes, baseline.live_block_bytes);
    EXPECT_EQ(stats.reserved_bytes, baseline.reserved_bytes);
    EXPECT_EQ(stats.metadata_bytes, baseline.metadata_bytes);
    EXPECT_EQ(stats.allocations, baseline.allocations);
    EXPECT_EQ(stats.in_place_resizes, usize(4));
    EXPECT_EQ(stats.grows, usize(2));
    EXPECT_EQ(stats.shrinks, usize(2));
    EXPECT_EQ(stats.copied_bytes, usize());
    EXPECT_EQ(pool.trim().slabs, usize());
    api.deallocate(zeroed.pointer, full);
    EXPECT_EQ(pool.statistics().live_bytes, usize());
    EXPECT_EQ(pool.statistics().live_block_bytes, usize());
    pool.trim();
    EXPECT_EQ(pool.statistics().reserved_bytes, usize());
}

TEST(PoolAllocator, ResizesInPlaceWithoutChangingSlotOrUpstreamAccounting) {
    PoolAllocator<> pool(PoolOptions { .slab_bytes = usize(1024) });
    exercise_pool_in_place(pool);
}

TEST(PoolAllocator, SharedOwnerForwardsInPlaceGrowZeroedAndShrink) {
    auto pool = SharedPoolAllocator<>::try_make(PoolOptions { .slab_bytes = usize(1024) }).unwrap();
    exercise_pool_in_place(pool);
}

TEST(PoolAllocator, DedicatedBlockKeepsItsCapacityAcrossShrinkAndRegrow) {
    auto                              upstream = BudgetAllocator<>::try_make(usize(4096)).unwrap();
    PoolAllocator<decltype(upstream)> pool(PoolOptions { .max_pooled_block_bytes = usize() },
                                           upstream.clone());
    auto                              api = as<Allocator>(pool);
    auto original                         = Layout::from_size_align(usize(512), usize(64)).unwrap();
    auto small                            = Layout::from_size_align(usize(8), usize(8)).unwrap();
    auto grown_layout                     = Layout::from_size_align(usize(384), usize(64)).unwrap();
    auto initial                          = api.allocate(original).unwrap();
    rstd::mem::memset(initial.pointer, u8(57), original.size);
    auto reserved = upstream.statistics().live_bytes;
    auto shrunk   = api.shrink(initial.pointer, original, small).unwrap();
    auto grown    = api.grow_zeroed(shrunk.pointer, small, grown_layout).unwrap();
    EXPECT_EQ(shrunk.pointer, initial.pointer);
    EXPECT_EQ(grown.pointer, initial.pointer);
    EXPECT_EQ(upstream.statistics().live_bytes, reserved);
    EXPECT_EQ(pool.statistics().allocations, usize(1));
    EXPECT_EQ(pool.statistics().in_place_resizes, usize(2));
    EXPECT_EQ(pool.statistics().copied_bytes, usize());
    for (unsigned i = 0; i < 384; ++i)
        EXPECT_EQ(static_cast<unsigned char*>(grown.pointer)[i], i < 8 ? 57 : 0);
    api.deallocate(grown.pointer, grown_layout);
    EXPECT_EQ(upstream.statistics().live_bytes, usize());
    EXPECT_EQ(pool.statistics().slabs, usize());
}

auto incompatible_pool_alignment(void* pointer) -> usize {
    usize alignment(32);
    while (reinterpret_cast<uintptr_t>(pointer) % alignment.to_primitive() == 0)
        alignment *= usize(2);
    return alignment;
}

TEST(PoolAllocator, FailedMovingResizesPreservePayloadAndAccounting) {
    auto                              upstream = BudgetAllocator<>::try_make(usize(1024)).unwrap();
    PoolAllocator<decltype(upstream)> pool(PoolOptions { .slab_bytes = usize(1024) },
                                           upstream.clone());
    auto                              api = as<Allocator>(pool);
    auto original                         = Layout::from_size_align(usize(17), usize(16)).unwrap();
    auto same_slot                        = Layout::from_size_align(usize(31), usize(16)).unwrap();
    auto larger                           = Layout::from_size_align(usize(65), usize(16)).unwrap();
    auto huge    = Layout::from_size_align(usize(2048), usize(64)).unwrap();
    auto initial = api.allocate(original).unwrap();
    rstd::mem::memset(initial.pointer, u8(111), original.size);
    auto grown = api.grow(initial.pointer, original, same_slot).unwrap();
    EXPECT_EQ(grown.pointer, initial.pointer);
    auto before = pool.statistics();
    EXPECT_TRUE(api.grow(grown.pointer, same_slot, larger).is_err());
    EXPECT_TRUE(api.grow_zeroed(grown.pointer, same_slot, huge).is_err());
    auto aligned =
        Layout::from_size_align(usize(8), incompatible_pool_alignment(grown.pointer)).unwrap();
    EXPECT_TRUE(api.shrink(grown.pointer, same_slot, aligned).is_err());
    for (unsigned i = 0; i < 17; ++i) EXPECT_EQ(static_cast<unsigned char*>(grown.pointer)[i], 111);
    auto after = pool.statistics();
    EXPECT_EQ(after.live_bytes, before.live_bytes);
    EXPECT_EQ(after.live_block_bytes, before.live_block_bytes);
    EXPECT_EQ(after.peak_live_bytes, before.peak_live_bytes);
    EXPECT_EQ(after.metadata_bytes, before.metadata_bytes);
    EXPECT_EQ(after.reserved_bytes, before.reserved_bytes);
    EXPECT_EQ(after.allocations, before.allocations);
    EXPECT_EQ(after.in_place_resizes, usize(1));
    EXPECT_EQ(after.grows, usize(3));
    EXPECT_EQ(after.shrinks, usize(1));
    EXPECT_EQ(after.failures, usize(3));
    EXPECT_EQ(after.copied_bytes, usize());
    api.deallocate(grown.pointer, same_slot);
    pool.trim();
    EXPECT_EQ(upstream.statistics().live_bytes, usize());
}

TEST(PoolAllocator, MovingResizeCopiesOnlyPreservedPrefixAndHonorsNewAlignment) {
    PoolAllocator<> pool(PoolOptions { .slab_bytes = usize(1024) });
    auto            api      = as<Allocator>(pool);
    auto            original = Layout::from_size_align(usize(17), usize(16)).unwrap();
    auto            larger   = Layout::from_size_align(usize(65), usize(16)).unwrap();
    auto            initial  = api.allocate(original).unwrap();
    rstd::mem::memset(initial.pointer, u8(63), original.size);
    auto grown = api.grow_zeroed(initial.pointer, original, larger).unwrap();
    EXPECT_NE(grown.pointer, initial.pointer);
    for (unsigned i = 0; i < 65; ++i)
        EXPECT_EQ(static_cast<unsigned char*>(grown.pointer)[i], i < 17 ? 63 : 0);
    auto aligned =
        Layout::from_size_align(usize(8), incompatible_pool_alignment(grown.pointer)).unwrap();
    auto shrunk = api.shrink(grown.pointer, larger, aligned).unwrap();
    EXPECT_NE(shrunk.pointer, grown.pointer);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(shrunk.pointer) % aligned.align.to_primitive(), 0u);
    for (unsigned i = 0; i < 8; ++i) EXPECT_EQ(static_cast<unsigned char*>(shrunk.pointer)[i], 63);
    EXPECT_EQ(pool.statistics().copied_bytes, usize(25));
    EXPECT_EQ(pool.statistics().in_place_resizes, usize());
    EXPECT_EQ(pool.statistics().live_bytes, usize(8));
    EXPECT_EQ(pool.statistics().peak_live_bytes, usize(82));
    api.deallocate(shrunk.pointer, aligned);
    pool.trim();
    EXPECT_EQ(pool.statistics().reserved_bytes, usize());
}

TEST(PoolAllocator, ZeroSizedResizeDoesNotReadHeadersOrCreateSlabs) {
    PoolAllocator<> pool(PoolOptions { .slab_bytes = usize(1024) });
    auto            api         = as<Allocator>(pool);
    auto            empty       = Layout::from_size_align(usize(), usize(256)).unwrap();
    auto            other_empty = Layout::from_size_align(usize(), usize(512)).unwrap();
    auto            full        = Layout::from_size_align(usize(17), usize(16)).unwrap();
    auto            initial     = api.allocate(empty).unwrap();
    EXPECT_EQ(initial.pointer, empty.dangling());
    EXPECT_EQ(pool.statistics().slabs, usize());
    auto grown = api.grow_zeroed(initial.pointer, empty, full).unwrap();
    for (unsigned i = 0; i < 17; ++i) EXPECT_EQ(static_cast<unsigned char*>(grown.pointer)[i], 0);
    auto shrunk = api.shrink(grown.pointer, full, empty).unwrap();
    EXPECT_EQ(shrunk.pointer, empty.dangling());
    auto moved_empty = api.grow(shrunk.pointer, empty, other_empty).unwrap();
    EXPECT_EQ(moved_empty.pointer, other_empty.dangling());
    api.deallocate(moved_empty.pointer, other_empty);
    EXPECT_EQ(pool.statistics().live_bytes, usize());
    EXPECT_EQ(pool.statistics().live_block_bytes, usize());
    EXPECT_EQ(pool.statistics().allocations, usize(1));
    EXPECT_EQ(pool.statistics().copied_bytes, usize());
    pool.trim();
    EXPECT_EQ(pool.statistics().reserved_bytes, usize());
}

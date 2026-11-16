#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;
using rstd::alloc::Allocator;
using rstd::alloc::BudgetAllocator;
using rstd::alloc::Layout;
using rstd::alloc::PoolAllocator;
using rstd::alloc::PoolOptions;
using rstd::alloc::PoolSizeClasses;
using rstd::alloc::SharedPoolAllocator;
using rstd::alloc::VirtualMemoryAllocator;

TEST(PoolAllocator, DefaultSizeClassesRemainPowersOfTwo) {
    PoolAllocator<> pool;
    PoolAllocator<> explicit_pool(PoolOptions { .size_classes = PoolSizeClasses::PowersOfTwo });
    EXPECT_EQ(PoolOptions {}.size_classes, PoolSizeClasses::PowersOfTwo);
    EXPECT_EQ(PoolAllocator<>::MAX_CLASSES, 68u);
    EXPECT_EQ(PoolOptions {}.max_pooled_block_bytes, usize(65536));
    EXPECT_EQ(pool.class_count(), usize(13));
    EXPECT_EQ(explicit_pool.class_count(), usize(13));
    unsigned previous = 0;
    for (unsigned index = 0, capacity = 16; index < 13; ++index, capacity *= 2) {
        for (auto size : { previous + 1, capacity }) {
            auto layout = Layout::from_size_align(usize(size), usize(16)).unwrap();
            auto result = pool.classify(layout).unwrap();
            EXPECT_EQ(result.index, usize(index));
            EXPECT_EQ(result.capacity, usize(capacity));
            auto explicit_result = explicit_pool.classify(layout).unwrap();
            EXPECT_EQ(result.index, explicit_result.index);
            EXPECT_EQ(result.capacity, explicit_result.capacity);
        }
        previous = capacity;
    }
    EXPECT_EQ(pool.statistics().slabs, usize());
}

TEST(PoolAllocator, SizeBasedSlabsScaleWithBucketCapacity) {
    PoolOptions     options { .size_classes           = PoolSizeClasses::Fine,
                              .target_blocks_per_slab = usize(64),
                              .slab_granularity       = usize(4096) };
    PoolAllocator<> pool(options);
    auto            small       = Layout::from_size_align(usize(176), usize(16)).unwrap();
    auto            first       = pool.allocate(small).unwrap();
    auto            small_stats = pool.statistics();
    EXPECT_EQ(small_stats.reserved_bytes, options.min_slab_bytes);
    auto medium       = Layout::from_size_align(usize(1024), usize(16)).unwrap();
    auto second       = pool.allocate(medium).unwrap();
    auto medium_bytes = pool.statistics().reserved_bytes - small_stats.reserved_bytes;
    EXPECT_GT(medium_bytes, options.min_slab_bytes);
    EXPECT_LT(medium_bytes, options.slab_bytes);
    EXPECT_EQ(medium_bytes % options.slab_granularity, usize());
    auto  large        = Layout::from_size_align(usize(50 * 1024), usize(16)).unwrap();
    auto  before_large = pool.statistics();
    void* blocks[4] {};
    for (auto& block : blocks) block = pool.allocate(large).unwrap().pointer;
    auto large_bytes = pool.statistics().reserved_bytes - before_large.reserved_bytes;
    EXPECT_GT(large_bytes, medium_bytes);
    EXPECT_LT(large_bytes, options.slab_bytes);
    EXPECT_EQ(large_bytes % options.slab_granularity, usize());
    EXPECT_EQ(pool.statistics().slabs, before_large.slabs + usize(1));
    auto extra = pool.allocate(large).unwrap();
    EXPECT_EQ(pool.statistics().slabs, before_large.slabs + usize(2));
    for (auto block : blocks) pool.deallocate(block, large);
    pool.deallocate(extra.pointer, large);
    pool.deallocate(second.pointer, medium);
    pool.deallocate(first.pointer, small);
    pool.trim();
    EXPECT_EQ(pool.statistics().reserved_bytes, usize());
}

TEST(PoolAllocator, FixedSlabDefaultsAndExplicitOptOutKeepTheirSize) {
    const usize targets[] { usize(), usize(64) };
    for (auto target : targets) {
        PoolOptions     options { .slab_bytes             = usize(4096),
                                  .target_blocks_per_slab = target,
                                  .min_slab_bytes         = usize(16384),
                                  .slab_granularity       = usize(4096) };
        PoolAllocator<> pool(options);
        auto            layout = Layout::from_size_align(usize(64), usize(16)).unwrap();
        auto            block  = pool.allocate(layout).unwrap();
        EXPECT_EQ(pool.statistics().reserved_bytes, options.slab_bytes);
        pool.deallocate(block.pointer, layout);
    }
    PoolAllocator<> pool;
    auto            layout = Layout::from_size_align(usize(64), usize(16)).unwrap();
    auto            block  = pool.allocate(layout).unwrap();
    EXPECT_EQ(pool.statistics().reserved_bytes, PoolOptions {}.slab_bytes);
    pool.deallocate(block.pointer, layout);
}

TEST(PoolAllocator, SizeBasedSlotTargetIsCappedWithoutOverflow) {
    PoolOptions     options { .slab_bytes             = usize(4096),
                              .target_blocks_per_slab = usize::MAX,
                              .slab_granularity       = usize(4096) };
    PoolAllocator<> pool(options);
    auto            layout = Layout::from_size_align(usize(512), usize(16)).unwrap();
    auto            block  = pool.allocate(layout).unwrap();
    EXPECT_EQ(pool.statistics().reserved_bytes, options.slab_bytes);
    EXPECT_EQ(pool.statistics().failures, usize());
    pool.deallocate(block.pointer, layout);
}

TEST(PoolAllocator, InvalidSlabGranularityDoesNotAllocate) {
    const usize granularities[] { usize(), usize(3) };
    for (auto granularity : granularities) {
        PoolAllocator<> pool(PoolOptions { .slab_granularity = granularity });
        auto            layout = Layout::from_size_align(usize(64), usize(16)).unwrap();
        EXPECT_TRUE(pool.allocate(layout).is_err());
        EXPECT_EQ(pool.statistics().reserved_bytes, usize());
        EXPECT_EQ(pool.statistics().allocations, usize());
        EXPECT_EQ(pool.statistics().failures, usize(1));
    }
}

TEST(PoolAllocator, FineSizeClassesCoverEveryBoundary) {
    PoolAllocator<> pool(
        PoolOptions { .slab_bytes = usize(1024), .size_classes = PoolSizeClasses::Fine });
    const unsigned capacities[] {
        16,    32,    48,    64,    80,    96,    112,   128,   144,   160,   176,   192,   208,
        224,   240,   256,   320,   384,   448,   512,   576,   640,   704,   768,   832,   896,
        960,   1024,  1280,  1536,  1792,  2048,  2304,  2560,  2816,  3072,  3328,  3584,  3840,
        4096,  5120,  6144,  7168,  8192,  9216,  10240, 11264, 12288, 13312, 14336, 15360, 16384,
        20480, 24576, 28672, 32768, 36864, 40960, 45056, 49152, 53248, 57344, 61440, 65536
    };
    EXPECT_EQ(pool.class_count(), usize(64));
    unsigned previous = 0;
    for (unsigned index = 0; index < 64; ++index) {
        for (auto size : { previous + 1, capacities[index] - 1, capacities[index] }) {
            for (unsigned alignment = 1; alignment <= 16; alignment *= 2) {
                auto layout = Layout::from_size_align(usize(size), usize(alignment)).unwrap();
                auto result = pool.classify(layout).unwrap();
                EXPECT_EQ(result.index, usize(index));
                EXPECT_EQ(result.capacity, usize(capacities[index]));
            }
        }
        auto first_layout = Layout::from_size_align(usize(previous + 1), usize(16)).unwrap();
        auto full_layout  = Layout::from_size_align(usize(capacities[index]), usize(16)).unwrap();
        auto next_layout =
            Layout::from_size_align(usize(capacities[index] + 1), usize(16)).unwrap();
        auto first                                    = pool.allocate_zeroed(first_layout).unwrap();
        static_cast<unsigned char*>(first.pointer)[0] = 57;
        auto full = pool.grow_zeroed(first.pointer, first_layout, full_layout).unwrap();
        EXPECT_EQ(full.pointer, first.pointer);
        auto next = pool.grow_zeroed(full.pointer, full_layout, next_layout).unwrap();
        EXPECT_NE(next.pointer, full.pointer);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(next.pointer) % 16, 0u);
        for (unsigned i = 0; i <= capacities[index]; ++i)
            EXPECT_EQ(static_cast<unsigned char*>(next.pointer)[i], i == 0 ? 57 : 0);
        pool.deallocate(next.pointer, next_layout);
        pool.trim();
        previous = capacities[index];
    }
    EXPECT_EQ(pool.statistics().slabs, usize());
}

TEST(PoolAllocator, ClassificationRejectsZeroOversizedOveralignedAndUnpoolableCapacity) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolAllocator<> pool(PoolOptions { .size_classes = mode });
        EXPECT_TRUE(pool.classify(Layout::from_size_align(usize(), usize(1)).unwrap()).is_none());
        EXPECT_TRUE(
            pool.classify(Layout::from_size_align(usize(65537), usize(16)).unwrap()).is_none());
        EXPECT_TRUE(
            pool.classify(Layout::from_size_align(usize(16), usize(32)).unwrap()).is_none());
        PoolAllocator<> disabled(
            PoolOptions { .max_pooled_block_bytes = usize(), .size_classes = mode });
        EXPECT_EQ(disabled.class_count(), usize());
        EXPECT_TRUE(
            disabled.classify(Layout::from_size_align(usize(1), usize(1)).unwrap()).is_none());
        PoolAllocator<> limited(
            PoolOptions { .max_pooled_block_bytes = usize(1000), .size_classes = mode });
        unsigned last = mode == PoolSizeClasses::Fine ? 960 : 512;
        EXPECT_EQ(limited.class_count(), usize(mode == PoolSizeClasses::Fine ? 27 : 6));
        EXPECT_EQ(limited.classify(Layout::from_size_align(usize(last), usize(16)).unwrap())
                      .unwrap()
                      .capacity,
                  usize(last));
        EXPECT_TRUE(limited.classify(Layout::from_size_align(usize(last + 1), usize(16)).unwrap())
                        .is_none());
        EXPECT_TRUE(
            limited.classify(Layout::from_size_align(usize(1000), usize(16)).unwrap()).is_none());
        PoolAllocator<> unlimited(
            PoolOptions { .max_pooled_block_bytes = usize::MAX, .size_classes = mode });
        EXPECT_TRUE(unlimited.classify(Layout::from_size_align(usize(131073), usize(16)).unwrap())
                        .is_none());
        EXPECT_EQ(unlimited.class_count(), usize(mode == PoolSizeClasses::Fine ? 68 : 14));
    }
}

TEST(PoolAllocator, ExtendedSizeClassesPreserveIndicesAndHonorConfiguredLimit) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolAllocator<> pool(
            PoolOptions { .max_pooled_block_bytes = usize(131072), .size_classes = mode });
        auto previous = usize(65536);
        auto count    = mode == PoolSizeClasses::Fine ? 4u : 1u;
        for (unsigned index = 0; index < count; ++index) {
            auto capacity = mode == PoolSizeClasses::Fine ? 81920u + index * 16384u : 131072u;
            for (auto size : { previous.to_primitive() + 1,
                               rstd::size_t(capacity - 1),
                               rstd::size_t(capacity) }) {
                auto layout = Layout::from_size_align(usize(size), usize(16)).unwrap();
                auto bucket = pool.classify(layout).unwrap();
                EXPECT_EQ(bucket.index, usize(index + (mode == PoolSizeClasses::Fine ? 64 : 13)));
                EXPECT_EQ(bucket.capacity, usize(capacity));
                EXPECT_EQ(pool.good_size(layout), bucket.capacity);
            }
            previous = usize(capacity);
        }
        EXPECT_EQ(pool.class_count(), usize(mode == PoolSizeClasses::Fine ? 68 : 14));
        EXPECT_TRUE(
            pool.classify(Layout::from_size_align(usize(131073), usize(16)).unwrap()).is_none());
        EXPECT_EQ(pool.statistics().allocations, usize());
        EXPECT_EQ(pool.statistics().reserved_bytes, usize());
        const unsigned limits[] {
            0, 15, 16, 17, 65535, 65536, 65537, 81919, 81920, 131071, 131072
        };
        const unsigned fine[] { 0, 0, 1, 1, 63, 64, 64, 64, 65, 67, 68 };
        const unsigned powers[] { 0, 0, 1, 1, 12, 13, 13, 13, 13, 13, 14 };
        for (unsigned index = 0; index < 11; ++index) {
            PoolAllocator<> limited(PoolOptions { .max_pooled_block_bytes = usize(limits[index]),
                                                  .size_classes           = mode });
            EXPECT_EQ(limited.class_count(),
                      usize(mode == PoolSizeClasses::Fine ? fine[index] : powers[index]));
        }
    }
}

TEST(PoolAllocator, GoodSizeDoesNotAllocateAndPreservesDedicatedRequestSizes) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolAllocator<VirtualMemoryAllocator> pool(
            PoolOptions { .max_pooled_block_bytes = usize(131072), .size_classes = mode });
        auto empty        = Layout::from_size_align(usize(), usize(256)).unwrap();
        auto small        = Layout::from_size_align(usize(17), usize(16)).unwrap();
        auto over_aligned = Layout::from_size_align(usize(17), usize(32)).unwrap();
        auto oversized    = Layout::from_size_align(usize(131073), usize(16)).unwrap();
        auto maximum = Layout::from_size_align(usize(isize::MAX.to_primitive()), usize(1)).unwrap();
        EXPECT_EQ(pool.good_size(empty), usize());
        EXPECT_EQ(pool.good_size(small), usize(32));
        EXPECT_EQ(pool.good_size(over_aligned), over_aligned.size());
        EXPECT_EQ(pool.good_size(oversized), oversized.size());
        EXPECT_EQ(pool.good_size(maximum), maximum.size());
        auto stats = pool.statistics();
        EXPECT_EQ(stats.slabs, usize());
        EXPECT_EQ(stats.allocations, usize());
        EXPECT_EQ(stats.failures, usize());
        EXPECT_EQ(stats.reserved_bytes, usize());
        EXPECT_EQ(pool.upstream_statistics().allocation_calls, usize());
        PoolAllocator<> disabled(
            PoolOptions { .max_pooled_block_bytes = usize(), .size_classes = mode });
        EXPECT_EQ(disabled.good_size(small), small.size());
    }
}

TEST(PoolAllocator, ExtendedHotspotsReuseSlabsAndTrimVirtualMemory) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        for (auto size : { 65544u, 87360u, 131072u }) {
            PoolAllocator<VirtualMemoryAllocator> pool(
                PoolOptions { .slab_bytes             = usize(524288),
                              .max_pooled_block_bytes = usize(131072),
                              .size_classes           = mode,
                              .target_blocks_per_slab = usize(2),
                              .slab_granularity       = usize(4096) });
            auto layout   = Layout::from_size_align(usize(size), usize(16)).unwrap();
            auto capacity = mode == PoolSizeClasses::PowersOfTwo ? 131072u
                            : size == 65544u                     ? 81920u
                            : size == 87360u                     ? 98304u
                                                                 : 131072u;
            auto first    = pool.allocate(layout).unwrap();
            auto second   = pool.allocate(layout).unwrap();
            EXPECT_EQ(first.size, layout.size());
            EXPECT_EQ(pool.allocation_size(first.pointer), layout.size());
            EXPECT_EQ(pool.usable_size(first.pointer), usize(capacity));
            EXPECT_EQ(pool.good_size(layout), usize(capacity));
            EXPECT_EQ(pool.statistics().live_block_bytes, (usize(capacity) + usize(16)) * usize(2));
            EXPECT_EQ(pool.statistics().slabs, usize(1));
            EXPECT_EQ(pool.upstream_statistics().allocation_calls, usize(1));
            rstd::mem::memset(second.pointer, u8(57), layout.size());
            pool.deallocate(first.pointer, layout);
            auto reused = pool.allocate(layout).unwrap();
            EXPECT_EQ(reused.pointer, first.pointer);
            EXPECT_EQ(pool.upstream_statistics().allocation_calls, usize(1));
            EXPECT_EQ(pool.trim().slabs, usize());
            EXPECT_EQ(static_cast<unsigned char*>(second.pointer)[size - 1], 57);
            pool.deallocate(reused.pointer, layout);
            pool.deallocate(second.pointer, layout);
            EXPECT_GT(pool.statistics().retained_bytes, usize());
            auto from_empty = pool.allocate(layout).unwrap();
            EXPECT_EQ(from_empty.pointer, second.pointer);
            EXPECT_EQ(pool.allocation_size(from_empty.pointer), layout.size());
            EXPECT_EQ(pool.usable_size(from_empty.pointer), usize(capacity));
            EXPECT_EQ(pool.upstream_statistics().allocation_calls, usize(1));
            pool.deallocate(from_empty.pointer, layout);
            auto before_trim = pool.statistics();
            auto trimmed     = pool.trim();
            EXPECT_EQ(trimmed.slabs, usize(1));
            EXPECT_EQ(trimmed.bytes, before_trim.reserved_bytes);
            EXPECT_EQ(pool.statistics().reserved_bytes, usize());
            EXPECT_EQ(pool.statistics().retained_bytes, usize());
            EXPECT_EQ(pool.statistics().metadata_bytes, usize());
            EXPECT_EQ(pool.upstream_statistics().mappings, usize());
            EXPECT_EQ(pool.upstream_statistics().mapped_bytes, usize());
        }
    }
}

TEST(PoolAllocator, AboveExtendedBoundaryUsesDedicatedCapacityAndReleasesImmediately) {
    PoolAllocator<VirtualMemoryAllocator> pool(PoolOptions {
        .max_pooled_block_bytes = usize(131072), .size_classes = PoolSizeClasses::Fine });
    auto layout = Layout::from_size_align(usize(131073), usize(16)).unwrap();
    auto block  = pool.allocate(layout).unwrap();
    EXPECT_EQ(block.size, layout.size());
    EXPECT_EQ(pool.allocation_size(block.pointer), layout.size());
    EXPECT_EQ(pool.usable_size(block.pointer), layout.size());
    EXPECT_EQ(pool.trim().slabs, usize());
    pool.deallocate(block.pointer, layout);
    EXPECT_EQ(pool.statistics().retained_bytes, usize());
    EXPECT_EQ(pool.statistics().reserved_bytes, usize());
    EXPECT_EQ(pool.upstream_statistics().mapped_bytes, usize());
    EXPECT_EQ(pool.upstream_statistics().mappings, usize());
}

TEST(PoolAllocator, FineLargeBlockUsesLessSlotCapacityThanDefault) {
    PoolAllocator<> fine(PoolOptions { .size_classes = PoolSizeClasses::Fine });
    PoolAllocator<> powers;
    auto            layout = Layout::from_size_align(usize(51200), usize(16)).unwrap();
    auto            first  = fine.allocate(layout).unwrap();
    auto            second = powers.allocate(layout).unwrap();
    EXPECT_EQ(fine.classify(layout).unwrap().capacity, usize(53248));
    EXPECT_EQ(powers.classify(layout).unwrap().capacity, usize(65536));
    EXPECT_EQ(fine.statistics().live_block_bytes + usize(12288),
              powers.statistics().live_block_bytes);
    fine.deallocate(first.pointer, layout);
    powers.deallocate(second.pointer, layout);
}

TEST(PoolAllocator, OveralignedRequestsBypassBothSizeClassModes) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolAllocator<> pool(PoolOptions { .size_classes = mode });
        auto            layout = Layout::from_size_align(usize(48), usize(64)).unwrap();
        auto            block  = pool.allocate(layout).unwrap();
        EXPECT_EQ(reinterpret_cast<uintptr_t>(block.pointer) % 64, 0u);
        EXPECT_EQ(pool.statistics().retained_bytes, usize());
        EXPECT_EQ(pool.trim().slabs, usize());
        pool.deallocate(block.pointer, layout);
        EXPECT_EQ(pool.statistics().slabs, usize());
        EXPECT_EQ(pool.statistics().reserved_bytes, usize());
        EXPECT_EQ(pool.statistics().metadata_bytes, usize());
    }
}

TEST(PoolAllocator, PartialSlabIsReusedBeforeMoreRecentlyEmptiedSlab) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolAllocator<> pool(PoolOptions { .slab_bytes = usize(1024), .size_classes = mode });
        auto            layout = Layout::from_size_align(usize(256), usize(16)).unwrap();
        void*           blocks[64] {};
        unsigned        count = 0;
        do {
            blocks[count++] = pool.allocate(layout).unwrap().pointer;
        } while (count < 63 && pool.statistics().slabs == usize(1));
        EXPECT_EQ(pool.statistics().slabs, usize(2));
        auto* remaining = blocks[count - 1];
        auto* reusable  = pool.allocate(layout).unwrap().pointer;
        pool.deallocate(reusable, layout);
        for (unsigned i = 0; i + 1 < count; ++i) pool.deallocate(blocks[i], layout);
        auto before = pool.statistics();
        EXPECT_EQ(before.retained_bytes, usize(1024));
        auto allocation = pool.allocate(layout).unwrap();
        EXPECT_EQ(allocation.pointer, reusable);
        EXPECT_EQ(pool.statistics().retained_bytes, before.retained_bytes);
        EXPECT_EQ(pool.statistics().slabs, before.slabs);
        EXPECT_EQ(pool.statistics().reuses, before.reuses + usize(1));
        auto trimmed = pool.trim();
        EXPECT_EQ(trimmed.bytes, usize(1024));
        EXPECT_EQ(trimmed.slabs, usize(1));
        EXPECT_EQ(pool.statistics().slabs, usize(1));
        EXPECT_EQ(pool.statistics().live_bytes, usize(512));
        pool.deallocate(allocation.pointer, layout);
        pool.deallocate(remaining, layout);
        EXPECT_EQ(pool.trim().slabs, usize(1));
        EXPECT_EQ(pool.statistics().reserved_bytes, usize());
    }
}

TEST(PoolAllocator, SingleSlotSlabMovesBetweenFullAndEmptyLists) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolAllocator<> pool(PoolOptions { .slab_bytes = usize(1024), .size_classes = mode });
        auto            layout = Layout::from_size_align(usize(4096), usize(16)).unwrap();
        auto            first  = pool.allocate(layout).unwrap();
        EXPECT_EQ(pool.trim().slabs, usize());
        pool.deallocate(first.pointer, layout);
        auto retained = pool.statistics().retained_bytes;
        EXPECT_GT(retained, usize(4096));
        auto reused = pool.allocate(layout).unwrap();
        EXPECT_EQ(reused.pointer, first.pointer);
        EXPECT_EQ(pool.statistics().retained_bytes, usize());
        EXPECT_EQ(pool.trim().slabs, usize());
        pool.deallocate(reused.pointer, layout);
        EXPECT_EQ(pool.trim().bytes, retained);
        EXPECT_EQ(pool.statistics().slabs, usize());
        EXPECT_EQ(pool.statistics().metadata_bytes, usize());
    }
}

TEST(PoolAllocator, EmptyRetentionBudgetIsGlobalAcrossFineClasses) {
    PoolAllocator<> pool(PoolOptions { .slab_bytes     = usize(1024),
                                       .retained_bytes = usize(1024),
                                       .size_classes   = PoolSizeClasses::Fine });
    auto            small  = Layout::from_size_align(usize(48), usize(16)).unwrap();
    auto            large  = Layout::from_size_align(usize(80), usize(16)).unwrap();
    auto            first  = pool.allocate(small).unwrap();
    auto            second = pool.allocate(large).unwrap();
    EXPECT_EQ(pool.statistics().slabs, usize(2));
    pool.deallocate(first.pointer, small);
    EXPECT_EQ(pool.statistics().retained_bytes, usize(1024));
    pool.deallocate(second.pointer, large);
    EXPECT_EQ(pool.statistics().retained_bytes, usize(1024));
    EXPECT_EQ(pool.statistics().reserved_bytes, usize(1024));
    EXPECT_EQ(pool.statistics().released_bytes, usize(1024));
    EXPECT_EQ(pool.statistics().slabs, usize(1));
    auto before = pool.statistics();
    auto reused = pool.allocate(large).unwrap();
    EXPECT_EQ(reused.pointer, second.pointer);
    EXPECT_EQ(pool.statistics().reuses, before.reuses + usize(1));
    pool.deallocate(reused.pointer, large);
    EXPECT_EQ(pool.trim().bytes, usize(1024));
}

TEST(PoolAllocator, ColdClassesGiveWayToHotSlabsWithinRetentionBudget) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolOptions                           options { .max_pooled_block_bytes = usize(131072),
                                                        .size_classes           = mode,
                                                        .target_blocks_per_slab = usize(4),
                                                        .slab_granularity       = usize(4096) };
        PoolAllocator<VirtualMemoryAllocator> pool(options);
        for (unsigned size = 4096; size <= 65536; size += 1024) {
            auto  layout = Layout::from_size_align(usize(size), usize(16)).unwrap();
            void* blocks[8] {};
            for (auto& block : blocks) block = pool.allocate(layout).unwrap().pointer;
            for (auto block : blocks) {
                pool.deallocate(block, layout);
                EXPECT_LE(pool.statistics().retained_bytes, options.retained_bytes);
            }
        }
        auto cold = pool.statistics();
        EXPECT_GE(cold.retained_bytes, options.retained_bytes - options.slab_bytes);
        auto before        = pool.upstream_statistics();
        auto first_layout  = Layout::from_size_align(usize(65552), usize(16)).unwrap();
        auto second_layout = Layout::from_size_align(usize(87368), usize(16)).unwrap();
        for (unsigned iteration = 0; iteration < 32; ++iteration) {
            auto first  = pool.allocate(first_layout).unwrap();
            auto second = pool.allocate(second_layout).unwrap();
            EXPECT_EQ(pool.allocation_size(first.pointer), first_layout.size());
            EXPECT_EQ(pool.allocation_size(second.pointer), second_layout.size());
            pool.deallocate(first.pointer, first_layout);
            EXPECT_LE(pool.statistics().retained_bytes, options.retained_bytes);
            pool.deallocate(second.pointer, second_layout);
            EXPECT_LE(pool.statistics().retained_bytes, options.retained_bytes);
            EXPECT_EQ(pool.upstream_statistics().allocation_calls,
                      before.allocation_calls + usize(2));
        }
        EXPECT_EQ(pool.statistics().allocations - cold.allocations, usize(64));
        EXPECT_EQ(pool.statistics().reuses - cold.reuses, usize(62));
        EXPECT_GT(pool.upstream_statistics().deallocation_calls, before.deallocation_calls);
        auto retained = pool.statistics().retained_bytes;
        EXPECT_EQ(pool.trim().bytes, retained);
        EXPECT_EQ(pool.statistics().slabs, usize());
        EXPECT_EQ(pool.statistics().metadata_bytes, usize());
        EXPECT_EQ(pool.upstream_statistics().mapped_bytes, usize());
    }
}

TEST(PoolAllocator, RetentionEvictsByEmptyOrderRatherThanCreationOrder) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolAllocator<VirtualMemoryAllocator> pool(PoolOptions { .slab_bytes       = usize(4096),
                                                                 .retained_bytes   = usize(8192),
                                                                 .size_classes     = mode,
                                                                 .slab_granularity = usize(4096) });
        auto first_layout  = Layout::from_size_align(usize(17), usize(16)).unwrap();
        auto second_layout = Layout::from_size_align(usize(65), usize(16)).unwrap();
        auto third_layout  = Layout::from_size_align(usize(257), usize(16)).unwrap();
        auto first         = pool.allocate(first_layout).unwrap();
        auto second        = pool.allocate(second_layout).unwrap();
        auto third         = pool.allocate(third_layout).unwrap();
        pool.deallocate(third.pointer, third_layout);
        pool.deallocate(first.pointer, first_layout);
        pool.deallocate(second.pointer, second_layout);
        EXPECT_EQ(pool.statistics().retained_bytes, usize(8192));
        EXPECT_EQ(pool.upstream_statistics().deallocation_calls, usize(1));
        auto reused_first  = pool.allocate(first_layout).unwrap();
        auto reused_second = pool.allocate(second_layout).unwrap();
        EXPECT_EQ(reused_first.pointer, first.pointer);
        EXPECT_EQ(reused_second.pointer, second.pointer);
        EXPECT_EQ(pool.statistics().reuses, usize(2));
        EXPECT_EQ(pool.upstream_statistics().allocation_calls, usize(3));
        EXPECT_EQ(pool.trim().slabs, usize());
        pool.deallocate(reused_first.pointer, first_layout);
        pool.deallocate(reused_second.pointer, second_layout);
        EXPECT_EQ(pool.trim().bytes, usize(8192));
        EXPECT_EQ(pool.upstream_statistics().mapped_bytes, usize());
    }
}

TEST(PoolAllocator, EmptyQueueUnlinksMiddleTailAndHeadBeforeFifoTrim) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolAllocator<VirtualMemoryAllocator> pool(PoolOptions { .slab_bytes       = usize(4096),
                                                                 .retained_bytes   = usize(16384),
                                                                 .size_classes     = mode,
                                                                 .slab_granularity = usize(4096) });
        const unsigned                        sizes[] { 17, 65, 257 };
        Layout                                layouts[3];
        void*                                 blocks[3] {};
        for (unsigned index = 0; index < 3; ++index) {
            layouts[index] = Layout::from_size_align(usize(sizes[index]), usize(16)).unwrap();
            blocks[index]  = pool.allocate(layouts[index]).unwrap().pointer;
        }
        for (unsigned index = 0; index < 3; ++index) pool.deallocate(blocks[index], layouts[index]);
        const unsigned reused[] { 1, 2, 0 };
        for (unsigned index : reused) {
            auto block = pool.allocate(layouts[index]).unwrap();
            EXPECT_EQ(block.pointer, blocks[index]);
            rstd::mem::memset(block.pointer, u8(57), layouts[index].size());
        }
        EXPECT_EQ(pool.statistics().retained_bytes, usize());
        EXPECT_EQ(pool.trim().slabs, usize());
        const unsigned emptied[] { 2, 0, 1 };
        for (unsigned index : emptied) pool.deallocate(blocks[index], layouts[index]);
        EXPECT_EQ(pool.trim(usize(8192)).bytes, usize(4096));
        EXPECT_EQ(pool.statistics().retained_bytes, usize(8192));
        for (unsigned index = 0; index < 2; ++index) {
            auto block = pool.allocate(layouts[index]).unwrap();
            EXPECT_EQ(block.pointer, blocks[index]);
            EXPECT_EQ(static_cast<unsigned char*>(block.pointer)[sizes[index] - 1], 57);
        }
        EXPECT_EQ(pool.upstream_statistics().allocation_calls, usize(3));
        EXPECT_EQ(pool.statistics().reuses, usize(5));
        EXPECT_EQ(pool.trim().slabs, usize());
        pool.deallocate(blocks[1], layouts[1]);
        pool.deallocate(blocks[0], layouts[0]);
        EXPECT_EQ(pool.trim(usize(4096)).bytes, usize(4096));
        auto last = pool.allocate(layouts[0]).unwrap();
        EXPECT_EQ(last.pointer, blocks[0]);
        EXPECT_EQ(pool.upstream_statistics().allocation_calls, usize(3));
        pool.deallocate(last.pointer, layouts[0]);
        EXPECT_EQ(pool.trim().slabs, usize(1));
        EXPECT_EQ(pool.upstream_statistics().mapped_bytes, usize());
    }
}

TEST(PoolAllocator, OversizedEmptySlabDoesNotEvictSmallerCachedSlabs) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolAllocator<VirtualMemoryAllocator> pool(
            PoolOptions { .slab_bytes             = usize(4096),
                          .max_pooled_block_bytes = usize(131072),
                          .retained_bytes         = usize(8192),
                          .size_classes           = mode,
                          .slab_granularity       = usize(4096) });
        auto small  = Layout::from_size_align(usize(17), usize(16)).unwrap();
        auto medium = Layout::from_size_align(usize(65), usize(16)).unwrap();
        auto large  = Layout::from_size_align(usize(87368), usize(16)).unwrap();
        auto first  = pool.allocate(small).unwrap();
        auto second = pool.allocate(medium).unwrap();
        pool.deallocate(first.pointer, small);
        pool.deallocate(second.pointer, medium);
        auto huge = pool.allocate(large).unwrap();
        EXPECT_EQ(pool.statistics().retained_bytes, usize(8192));
        pool.deallocate(huge.pointer, large);
        EXPECT_EQ(pool.statistics().retained_bytes, usize(8192));
        EXPECT_EQ(pool.statistics().reserved_bytes, usize(8192));
        EXPECT_EQ(pool.upstream_statistics().deallocation_calls, usize(1));
        auto reused_first  = pool.allocate(small).unwrap();
        auto reused_second = pool.allocate(medium).unwrap();
        EXPECT_EQ(reused_first.pointer, first.pointer);
        EXPECT_EQ(reused_second.pointer, second.pointer);
        EXPECT_EQ(pool.statistics().reuses, usize(2));
        EXPECT_EQ(pool.upstream_statistics().allocation_calls, usize(3));
        pool.deallocate(reused_first.pointer, small);
        pool.deallocate(reused_second.pointer, medium);
        EXPECT_EQ(pool.trim().slabs, usize(2));
        EXPECT_EQ(pool.upstream_statistics().mapped_bytes, usize());
    }
}

TEST(PoolAllocator, EmptyQueueAndClassChainTrackMultipleSingleSlotSlabs) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolAllocator<VirtualMemoryAllocator> pool(PoolOptions { .slab_bytes       = usize(4096),
                                                                 .retained_bytes   = usize(16384),
                                                                 .size_classes     = mode,
                                                                 .slab_granularity = usize(4096) });
        auto layout = Layout::from_size_align(usize(4096), usize(16)).unwrap();
        auto first  = pool.allocate(layout).unwrap();
        auto second = pool.allocate(layout).unwrap();
        auto third  = pool.allocate(layout).unwrap();
        EXPECT_EQ(pool.statistics().slabs, usize(3));
        pool.deallocate(third.pointer, layout);
        pool.deallocate(first.pointer, layout);
        pool.deallocate(second.pointer, layout);
        EXPECT_EQ(pool.statistics().slabs, usize(2));
        EXPECT_EQ(pool.statistics().retained_bytes, usize(16384));
        auto reused_second = pool.allocate(layout).unwrap();
        auto reused_first  = pool.allocate(layout).unwrap();
        EXPECT_EQ(reused_second.pointer, second.pointer);
        EXPECT_EQ(reused_first.pointer, first.pointer);
        EXPECT_EQ(pool.statistics().reuses, usize(2));
        EXPECT_EQ(pool.upstream_statistics().allocation_calls, usize(3));
        EXPECT_EQ(pool.trim().slabs, usize());
        pool.deallocate(reused_first.pointer, layout);
        pool.deallocate(reused_second.pointer, layout);
        EXPECT_EQ(pool.trim(usize(8192)).bytes, usize(8192));
        auto last = pool.allocate(layout).unwrap();
        EXPECT_EQ(last.pointer, second.pointer);
        EXPECT_EQ(pool.statistics().reuses, usize(3));
        EXPECT_EQ(pool.upstream_statistics().allocation_calls, usize(3));
        pool.deallocate(last.pointer, layout);
        EXPECT_EQ(pool.trim().slabs, usize(1));
        EXPECT_EQ(pool.upstream_statistics().mapped_bytes, usize());
    }
}

TEST(PoolAllocator, ZeroRetentionBudgetLeavesNoEmptyQueueEntries) {
    PoolAllocator<VirtualMemoryAllocator> pool(PoolOptions {
        .slab_bytes = usize(4096), .retained_bytes = usize(), .slab_granularity = usize(4096) });
    auto layout = Layout::from_size_align(usize(17), usize(16)).unwrap();
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
        auto block = pool.allocate(layout).unwrap();
        pool.deallocate(block.pointer, layout);
        EXPECT_EQ(pool.statistics().retained_bytes, usize());
        EXPECT_EQ(pool.statistics().reserved_bytes, usize());
        EXPECT_EQ(pool.statistics().metadata_bytes, usize());
        EXPECT_EQ(pool.trim().slabs, usize());
        EXPECT_EQ(pool.upstream_statistics().mapped_bytes, usize());
    }
    EXPECT_EQ(pool.upstream_statistics().allocation_calls, usize(8));
    EXPECT_EQ(pool.upstream_statistics().deallocation_calls, usize(8));
}

TEST(PoolAllocator, TrimHonorsGlobalTargetWithoutReleasingPartialSlabs) {
    PoolAllocator<> pool(
        PoolOptions { .slab_bytes = usize(1024), .size_classes = PoolSizeClasses::Fine });
    auto first_layout  = Layout::from_size_align(usize(48), usize(16)).unwrap();
    auto second_layout = Layout::from_size_align(usize(80), usize(16)).unwrap();
    auto third_layout  = Layout::from_size_align(usize(112), usize(16)).unwrap();
    auto first         = pool.allocate(first_layout).unwrap();
    auto live          = pool.allocate(first_layout).unwrap();
    auto second        = pool.allocate(second_layout).unwrap();
    auto third         = pool.allocate(third_layout).unwrap();
    rstd::mem::memset(live.pointer, u8(57), first_layout.size());
    pool.deallocate(first.pointer, first_layout);
    pool.deallocate(second.pointer, second_layout);
    pool.deallocate(third.pointer, third_layout);
    EXPECT_EQ(pool.statistics().retained_bytes, usize(2048));
    auto retained = pool.trim(usize(1024));
    EXPECT_EQ(retained.bytes, usize(1024));
    EXPECT_EQ(retained.slabs, usize(1));
    EXPECT_EQ(pool.statistics().retained_bytes, usize(1024));
    EXPECT_EQ(pool.statistics().slabs, usize(2));
    EXPECT_EQ(pool.trim().slabs, usize(1));
    EXPECT_EQ(pool.statistics().reserved_bytes, usize(1024));
    EXPECT_EQ(pool.statistics().live_bytes, first_layout.size());
    for (unsigned i = 0; i < 48; ++i) EXPECT_EQ(static_cast<unsigned char*>(live.pointer)[i], 57);
    pool.deallocate(live.pointer, first_layout);
    EXPECT_EQ(pool.trim().slabs, usize(1));
    EXPECT_EQ(pool.statistics().released_bytes, usize(3072));
    EXPECT_EQ(pool.statistics().metadata_bytes, usize());
}

template<typename A>
void exercise_pool_in_place(const A& pool) {
    auto api          = as<Allocator>(pool);
    auto full         = Layout::from_size_align(usize(32), usize(16)).unwrap();
    auto small        = Layout::from_size_align(usize(17), usize(1)).unwrap();
    auto grown_layout = Layout::from_size_align(usize(31), usize(16)).unwrap();
    auto tiny         = Layout::from_size_align(usize(7), usize(8)).unwrap();
    auto initial      = api.allocate(full).unwrap();
    EXPECT_EQ(pool.allocation_size(initial.pointer), full.size());
    EXPECT_EQ(pool.usable_size(initial.pointer), pool.good_size(full));
    rstd::mem::memset(initial.pointer, u8(0xab), full.size());
    auto baseline = pool.statistics();
    auto shrunk   = api.shrink(initial.pointer, full, small).unwrap();
    EXPECT_EQ(shrunk.pointer, initial.pointer);
    EXPECT_EQ(shrunk.size, small.size());
    EXPECT_EQ(pool.allocation_size(shrunk.pointer), small.size());
    EXPECT_EQ(pool.usable_size(shrunk.pointer), full.size());
    EXPECT_EQ(pool.statistics().live_bytes, small.size());
    auto grown = api.grow(shrunk.pointer, small, grown_layout).unwrap();
    EXPECT_EQ(grown.pointer, initial.pointer);
    EXPECT_EQ(grown.size, grown_layout.size());
    EXPECT_EQ(pool.allocation_size(grown.pointer), grown_layout.size());
    EXPECT_EQ(pool.usable_size(grown.pointer), full.size());
    for (unsigned i = 0; i < 31; ++i)
        EXPECT_EQ(static_cast<unsigned char*>(grown.pointer)[i], 0xab);
    shrunk = api.shrink(grown.pointer, grown_layout, tiny).unwrap();
    EXPECT_EQ(pool.allocation_size(shrunk.pointer), tiny.size());
    EXPECT_EQ(pool.usable_size(shrunk.pointer), full.size());
    auto zeroed = api.grow_zeroed(shrunk.pointer, tiny, full).unwrap();
    EXPECT_EQ(zeroed.pointer, initial.pointer);
    EXPECT_EQ(pool.allocation_size(zeroed.pointer), full.size());
    EXPECT_EQ(pool.usable_size(zeroed.pointer), full.size());
    for (unsigned i = 0; i < 32; ++i)
        EXPECT_EQ(static_cast<unsigned char*>(zeroed.pointer)[i], i < 7 ? 0xab : 0);
    auto stats = pool.statistics();
    EXPECT_EQ(stats.live_bytes, full.size());
    EXPECT_EQ(stats.peak_live_bytes, full.size());
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
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        PoolAllocator<> pool(PoolOptions { .slab_bytes = usize(1024), .size_classes = mode });
        exercise_pool_in_place(pool);
    }
}

TEST(PoolAllocator, SharedOwnerForwardsInPlaceGrowZeroedAndShrink) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine }) {
        auto pool = SharedPoolAllocator<>::try_make(
                        PoolOptions { .slab_bytes = usize(1024), .size_classes = mode })
                        .unwrap();
        exercise_pool_in_place(pool);
    }
}

TEST(PoolAllocator, SharedOwnerSizeQueriesUseTheSamePoolAcrossClones) {
    auto pool    = SharedPoolAllocator<VirtualMemoryAllocator>::try_make(
                       PoolOptions { .max_pooled_block_bytes = usize(131072),
                                     .size_classes           = PoolSizeClasses::Fine })
                       .unwrap();
    auto clone   = pool.clone();
    auto layout  = Layout::from_size_align(usize(87360), usize(16)).unwrap();
    auto smaller = Layout::from_size_align(usize(65544), usize(16)).unwrap();
    EXPECT_EQ(clone.good_size(layout), usize(98304));
    EXPECT_EQ(pool.snapshot().upstream.allocation_calls, usize());
    auto block = clone.allocate(layout).unwrap();
    EXPECT_EQ(pool.allocation_size(block.pointer), layout.size());
    EXPECT_EQ(pool.usable_size(block.pointer), usize(98304));
    auto shrunk = pool.shrink(block.pointer, layout, smaller).unwrap();
    EXPECT_EQ(shrunk.pointer, block.pointer);
    EXPECT_EQ(clone.allocation_size(shrunk.pointer), smaller.size());
    EXPECT_EQ(clone.usable_size(shrunk.pointer), usize(98304));
    EXPECT_EQ(clone.good_size(smaller), usize(81920));
    EXPECT_EQ(clone.trim().slabs, usize());
    clone.deallocate(shrunk.pointer, smaller);
    EXPECT_EQ(pool.trim().slabs, usize(1));
    EXPECT_EQ(clone.snapshot().upstream.mapped_bytes, usize());
    EXPECT_EQ(clone.snapshot().pool.live_bytes, usize());
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
    EXPECT_EQ(pool.good_size(original), original.size());
    EXPECT_EQ(pool.allocation_size(initial.pointer), original.size());
    EXPECT_EQ(pool.usable_size(initial.pointer), original.size());
    rstd::mem::memset(initial.pointer, u8(57), original.size());
    auto reserved = upstream.statistics().live_bytes;
    auto shrunk   = api.shrink(initial.pointer, original, small).unwrap();
    EXPECT_EQ(pool.allocation_size(shrunk.pointer), small.size());
    EXPECT_EQ(pool.usable_size(shrunk.pointer), original.size());
    auto grown = api.grow_zeroed(shrunk.pointer, small, grown_layout).unwrap();
    EXPECT_EQ(pool.allocation_size(grown.pointer), grown_layout.size());
    EXPECT_EQ(pool.usable_size(grown.pointer), original.size());
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

void exercise_pool_failed_resizes(PoolSizeClasses size_classes) {
    auto                              upstream = BudgetAllocator<>::try_make(usize(1024)).unwrap();
    PoolAllocator<decltype(upstream)> pool(
        PoolOptions { .slab_bytes = usize(1024), .size_classes = size_classes }, upstream.clone());
    auto api       = as<Allocator>(pool);
    auto original  = Layout::from_size_align(usize(17), usize(16)).unwrap();
    auto same_slot = Layout::from_size_align(usize(31), usize(16)).unwrap();
    auto larger    = Layout::from_size_align(usize(65), usize(16)).unwrap();
    auto huge      = Layout::from_size_align(usize(2048), usize(64)).unwrap();
    auto initial   = api.allocate(original).unwrap();
    rstd::mem::memset(initial.pointer, u8(111), original.size());
    auto grown = api.grow(initial.pointer, original, same_slot).unwrap();
    EXPECT_EQ(grown.pointer, initial.pointer);
    EXPECT_EQ(pool.allocation_size(grown.pointer), same_slot.size());
    auto capacity = pool.usable_size(grown.pointer);
    auto before   = pool.statistics();
    EXPECT_TRUE(api.grow(grown.pointer, same_slot, larger).is_err());
    EXPECT_EQ(pool.allocation_size(grown.pointer), same_slot.size());
    EXPECT_EQ(pool.usable_size(grown.pointer), capacity);
    EXPECT_TRUE(api.grow_zeroed(grown.pointer, same_slot, huge).is_err());
    EXPECT_EQ(pool.allocation_size(grown.pointer), same_slot.size());
    EXPECT_EQ(pool.usable_size(grown.pointer), capacity);
    auto aligned =
        Layout::from_size_align(usize(8), incompatible_pool_alignment(grown.pointer)).unwrap();
    EXPECT_TRUE(api.shrink(grown.pointer, same_slot, aligned).is_err());
    EXPECT_EQ(pool.allocation_size(grown.pointer), same_slot.size());
    EXPECT_EQ(pool.usable_size(grown.pointer), capacity);
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

TEST(PoolAllocator, FailedMovingResizesPreservePayloadAndAccounting) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine })
        exercise_pool_failed_resizes(mode);
}

void exercise_pool_moving_resizes(PoolSizeClasses size_classes) {
    PoolAllocator<> pool(PoolOptions { .slab_bytes = usize(1024), .size_classes = size_classes });
    auto            api      = as<Allocator>(pool);
    auto            original = Layout::from_size_align(usize(17), usize(16)).unwrap();
    auto            larger   = Layout::from_size_align(usize(65), usize(16)).unwrap();
    auto            initial  = api.allocate(original).unwrap();
    rstd::mem::memset(initial.pointer, u8(63), original.size());
    auto grown = api.grow_zeroed(initial.pointer, original, larger).unwrap();
    EXPECT_EQ(pool.allocation_size(grown.pointer), larger.size());
    EXPECT_EQ(pool.usable_size(grown.pointer), pool.good_size(larger));
    EXPECT_NE(grown.pointer, initial.pointer);
    for (unsigned i = 0; i < 65; ++i)
        EXPECT_EQ(static_cast<unsigned char*>(grown.pointer)[i], i < 17 ? 63 : 0);
    auto aligned =
        Layout::from_size_align(usize(8), incompatible_pool_alignment(grown.pointer)).unwrap();
    auto shrunk = api.shrink(grown.pointer, larger, aligned).unwrap();
    EXPECT_EQ(pool.allocation_size(shrunk.pointer), aligned.size());
    EXPECT_EQ(pool.usable_size(shrunk.pointer), aligned.size());
    EXPECT_NE(shrunk.pointer, grown.pointer);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(shrunk.pointer) % aligned.align().to_primitive(), 0u);
    for (unsigned i = 0; i < 8; ++i) EXPECT_EQ(static_cast<unsigned char*>(shrunk.pointer)[i], 63);
    EXPECT_EQ(pool.statistics().copied_bytes, usize(25));
    EXPECT_EQ(pool.statistics().in_place_resizes, usize());
    EXPECT_EQ(pool.statistics().live_bytes, usize(8));
    EXPECT_EQ(pool.statistics().peak_live_bytes, usize(82));
    api.deallocate(shrunk.pointer, aligned);
    pool.trim();
    EXPECT_EQ(pool.statistics().reserved_bytes, usize());
}

TEST(PoolAllocator, MovingResizeCopiesOnlyPreservedPrefixAndHonorsNewAlignment) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine })
        exercise_pool_moving_resizes(mode);
}

void exercise_pool_zero_sized_resizes(PoolSizeClasses size_classes) {
    PoolAllocator<> pool(PoolOptions { .slab_bytes = usize(1024), .size_classes = size_classes });
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

TEST(PoolAllocator, ZeroSizedResizeDoesNotReadHeadersOrCreateSlabs) {
    for (auto mode : { PoolSizeClasses::PowersOfTwo, PoolSizeClasses::Fine })
        exercise_pool_zero_sized_resizes(mode);
}

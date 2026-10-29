#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;

struct ArenaUpstreamState {
    struct Record {
        void*               pointer {};
        rstd::alloc::Layout requested {};
        rstd::alloc::Layout physical {};
    } records[128];
    int   allocations {};
    int   deallocations {};
    int   attempts {};
    int   fail_at {};
    usize live_bytes {};
    usize extra_bytes {};
    bool  release_layout_mismatch {};
};

struct ArenaTestUpstream {
    ArenaUpstreamState* state;
};

struct ArenaFailingUpstream {};

struct ArenaRcProbe {
    int* drops;

    explicit ArenaRcProbe(int& drops): drops(&drops) {}
    ~ArenaRcProbe() { ++*drops; }
};

namespace rstd
{

template<>
struct Impl<alloc::Allocator, ::ArenaTestUpstream>
    : DefaultInImpl<alloc::Allocator, ::ArenaTestUpstream> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        auto& state = *this->self().state;
        if (++state.attempts == state.fail_at) return Err(alloc::AllocError {});
        auto physical =
            alloc::Layout::from_size_align(layout.size + state.extra_bytes, layout.align).unwrap();
        auto allocation = as<alloc::Allocator>(::alloc::GLOBAL).allocate(physical);
        if (allocation.is_err()) return allocation;
        for (auto& record : state.records) {
            if (! record.pointer) {
                record = { allocation->pointer, layout, physical };
                ++state.allocations;
                state.live_bytes += layout.size;
                return allocation;
            }
        }
        as<alloc::Allocator>(::alloc::GLOBAL).deallocate(allocation->pointer, physical);
        return Err(alloc::AllocError {});
    }

    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        auto& state = *this->self().state;
        for (auto& record : state.records) {
            if (record.pointer == pointer) {
                state.release_layout_mismatch |=
                    record.requested.size != layout.size || record.requested.align != layout.align;
                ++state.deallocations;
                state.live_bytes -= record.requested.size;
                as<alloc::Allocator>(::alloc::GLOBAL).deallocate(pointer, record.physical);
                record.pointer = nullptr;
                return;
            }
        }
        state.release_layout_mismatch = true;
    }
};

template<>
struct Impl<alloc::Allocator, ::ArenaFailingUpstream>
    : DefaultInImpl<alloc::Allocator, ::ArenaFailingUpstream> {
    auto allocate(alloc::Layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        return Err(alloc::AllocError {});
    }

    void deallocate(void*, alloc::Layout) const noexcept {}
};

} // namespace rstd

TEST(Arena, UsesOrdinaryAndDedicatedSlabs) {
    ArenaUpstreamState upstream {};
    {
        alloc::BumpArena arena(usize(1024), ArenaTestUpstream { &upstream });
        auto             allocator = arena.allocator();

        auto first = rstd::as<rstd::alloc::Allocator>(allocator)
                         .allocate(rstd::alloc::Layout::array<u8>(usize(3)).unwrap())
                         .unwrap();
        auto aligned =
            rstd::as<rstd::alloc::Allocator>(allocator)
                .allocate(rstd::alloc::Layout::from_size_align(usize(8), usize(64)).unwrap())
                .unwrap();
        auto large = rstd::as<rstd::alloc::Allocator>(allocator)
                         .allocate(rstd::alloc::Layout::array<u8>(usize(600)).unwrap())
                         .unwrap();

        EXPECT_NE(first.pointer, nullptr);
        EXPECT_EQ(reinterpret_cast<uintptr_t>(aligned.pointer) % 64, 0u);
        EXPECT_NE(large.pointer, nullptr);

        auto stats = arena.stats();
        EXPECT_EQ(stats.used_bytes, usize(611));
        EXPECT_EQ(stats.ordinary_slabs, usize(2));
        EXPECT_EQ(stats.large_slabs, usize(1));
        EXPECT_EQ(stats.reserved_bytes, usize(2648) + stats.metadata_bytes);
        EXPECT_EQ(stats.reserved_bytes, upstream.live_bytes);
        EXPECT_GT(stats.metadata_bytes, usize());
        EXPECT_EQ(stats.peak_used_bytes, stats.used_bytes);
    }
    EXPECT_EQ(upstream.allocations, 3);
    EXPECT_EQ(upstream.deallocations, 3);
    EXPECT_FALSE(upstream.release_layout_mismatch);
}

TEST(Arena, ReportsUpstreamAllocationFailure) {
    alloc::BumpArena arena(usize(1024), ArenaFailingUpstream {});
    auto             allocator = arena.allocator();
    auto             result =
        rstd::as<rstd::alloc::Allocator>(allocator).allocate(rstd::alloc::Layout::make<int>());
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(arena.stats().reserved_bytes, usize());
}

TEST(Arena, VecGrowthRetainsPreviousBuffersUntilArenaDrop) {
    ArenaUpstreamState upstream {};
    {
        alloc::BumpArena arena(usize(1024), ArenaTestUpstream { &upstream });
        auto values = alloc::vec::Vec<int, decltype(arena.allocator())>::with_capacity_in(
            usize(1), arena.allocator());
        values.push(1);
        values.push(2);
        values.push(3);

        EXPECT_EQ(values.len(), usize(3));
        EXPECT_GT(arena.stats().used_bytes, values.capacity() * usize(sizeof(int)));
        EXPECT_EQ(upstream.deallocations, 0);
    }
    EXPECT_EQ(upstream.allocations, upstream.deallocations);
}

TEST(Arena, ResetRetainsAllOrdinarySlabsWithoutAllocating) {
    ArenaUpstreamState upstream {};
    {
        alloc::BumpArena arena(usize(1024), ArenaTestUpstream { &upstream });
        auto layout = rstd::alloc::Layout::from_size_align(usize(400), usize(16)).unwrap();
        auto large  = rstd::alloc::Layout::from_size_align(usize(700), usize(16)).unwrap();
        for (auto index = 0; index < 6; ++index) ASSERT_TRUE(arena.allocate(layout).is_ok());
        ASSERT_TRUE(arena.allocate(large).is_ok());
        auto peak        = arena.stats().peak_used_bytes;
        auto attempts    = upstream.attempts;
        upstream.fail_at = attempts + 1;

        arena.reset(alloc::ArenaReset::retain_capacity());
        EXPECT_EQ(upstream.attempts, attempts);
        EXPECT_EQ(upstream.deallocations, 1);
        EXPECT_EQ(arena.stats().ordinary_slabs, usize(3));
        EXPECT_EQ(arena.stats().large_slabs, usize());
        EXPECT_EQ(arena.stats().used_bytes, usize());
        EXPECT_EQ(arena.stats().padding_bytes, usize());
        EXPECT_EQ(arena.stats().peak_used_bytes, peak);
        for (auto index = 0; index < 6; ++index) ASSERT_TRUE(arena.allocate(layout).is_ok());
        EXPECT_EQ(upstream.attempts, attempts);
        EXPECT_EQ(arena.stats().reserved_bytes, upstream.live_bytes);
    }
    EXPECT_EQ(upstream.allocations, upstream.deallocations);
    EXPECT_FALSE(upstream.release_layout_mismatch);
}

TEST(Arena, ResetLimitCountsCompleteSlabsAndNeverAllocates) {
    ArenaUpstreamState upstream {};
    alloc::BumpArena   arena(usize(1024), ArenaTestUpstream { &upstream });
    auto layout = rstd::alloc::Layout::from_size_align(usize(512), usize(16)).unwrap();
    for (auto index = 0; index < 6; ++index) ASSERT_TRUE(arena.allocate(layout).is_ok());
    auto one_slab    = arena.stats().reserved_bytes / usize(3);
    auto attempts    = upstream.attempts;
    upstream.fail_at = attempts + 1;
    arena.reset(alloc::ArenaReset::retain_with_limit(one_slab));
    EXPECT_EQ(arena.stats().reserved_bytes, one_slab);
    EXPECT_EQ(arena.stats().ordinary_slabs, usize(1));
    EXPECT_EQ(upstream.live_bytes, one_slab);
    EXPECT_EQ(upstream.attempts, attempts);
    ASSERT_TRUE(arena.allocate(layout).is_ok());
    arena.reset(alloc::ArenaReset::retain_with_limit(usize(1024)));
    EXPECT_EQ(arena.stats().reserved_bytes, usize());
    EXPECT_EQ(upstream.live_bytes, usize());
    EXPECT_EQ(upstream.attempts, attempts);
    EXPECT_FALSE(upstream.release_layout_mismatch);
}

TEST(Arena, FreeAllRunsAfterContainersDropAndAllowsReuse) {
    ArenaUpstreamState upstream {};
    alloc::BumpArena   arena(usize(1024), ArenaTestUpstream { &upstream });
    {
        auto values = alloc::vec::Vec<int, decltype(arena.allocator())>::make_in(arena.allocator());
        values.push(42);
        EXPECT_EQ(values[usize()], 42);
    }
    arena.reset();
    EXPECT_EQ(upstream.live_bytes, usize());
    EXPECT_EQ(arena.stats().metadata_bytes, usize());
    ASSERT_TRUE(arena.allocate(rstd::alloc::Layout::make<int>()).is_ok());
    EXPECT_EQ(arena.stats().used_bytes, usize(sizeof(int)));
}

TEST(Arena, ZeroAndOveralignedRequestsPreserveReleaseLayouts) {
    ArenaUpstreamState upstream {};
    upstream.extra_bytes = usize(128);
    {
        alloc::BumpArena arena(usize(1024), ArenaTestUpstream { &upstream });
        auto             zero = rstd::alloc::Layout::from_size_align(usize(), usize(4096)).unwrap();
        auto             empty = arena.allocate(zero).unwrap();
        EXPECT_EQ(reinterpret_cast<uintptr_t>(empty.pointer) % 4096, 0u);
        EXPECT_EQ(upstream.attempts, 0);
        auto aligned = rstd::alloc::Layout::from_size_align(usize(17), usize(4096)).unwrap();
        auto value   = arena.allocate(aligned).unwrap();
        EXPECT_EQ(reinterpret_cast<uintptr_t>(value.pointer) % 4096, 0u);
        EXPECT_EQ(value.size, aligned.size);
        EXPECT_EQ(arena.stats().reserved_bytes, upstream.live_bytes);
        arena.reset(alloc::ArenaReset::retain_capacity());
        EXPECT_EQ(upstream.live_bytes, usize());
    }
    EXPECT_FALSE(upstream.release_layout_mismatch);
}

TEST(Arena, RejectsOverflowBeforeCallingUpstream) {
    ArenaUpstreamState upstream {};
    alloc::BumpArena   arena(usize(1024), ArenaTestUpstream { &upstream });
    auto               layout = rstd::alloc::Layout::from_size_align(usize::MAX, usize(1)).unwrap();
    EXPECT_TRUE(arena.allocate(layout).is_err());
    EXPECT_EQ(upstream.attempts, 0);
    EXPECT_EQ(arena.stats().reserved_bytes, usize());
}

TEST(RecyclingArena, ReusesReleasedLayoutsAndDropsAllSlabs) {
    ArenaUpstreamState upstream {};
    {
        alloc::RecyclingArena arena(usize(1024), ArenaTestUpstream { &upstream });
        auto                  allocator = arena.allocator();
        auto const layout = rstd::alloc::Layout::from_size_align(usize(128), usize(16)).unwrap();

        auto first = rstd::as<rstd::alloc::Allocator>(allocator).allocate(layout).unwrap();
        rstd::as<rstd::alloc::Allocator>(allocator).deallocate(first.pointer, layout);
        auto second = rstd::as<rstd::alloc::Allocator>(allocator).allocate(layout).unwrap();

        EXPECT_EQ(first.pointer, second.pointer);
        EXPECT_EQ(arena.stats().reuses, usize(1));
        EXPECT_EQ(arena.stats().reused_bytes, usize(128));
        EXPECT_EQ(arena.stats().live_bytes, usize(128));
        EXPECT_EQ(arena.stats().peak_live_bytes, usize(128));
        EXPECT_EQ(arena.stats().metadata_blocks, usize(1));
        EXPECT_GE(arena.stats().metadata_reserved_bytes, arena.stats().metadata_used_bytes);
        EXPECT_EQ(upstream.allocations, 2);
        rstd::as<rstd::alloc::Allocator>(allocator).deallocate(second.pointer, layout);
        EXPECT_EQ(arena.stats().live_bytes, usize());
    }
    EXPECT_EQ(upstream.allocations, upstream.deallocations);
}

TEST(RecyclingArena, SeparatesRecycledAllocationsBySizeAndAlignment) {
    alloc::RecyclingArena arena(usize(1024));
    auto                  allocator = arena.allocator();
    auto const small   = rstd::alloc::Layout::from_size_align(usize(32), usize(8)).unwrap();
    auto const aligned = rstd::alloc::Layout::from_size_align(usize(32), usize(64)).unwrap();
    auto const large   = rstd::alloc::Layout::from_size_align(usize(64), usize(8)).unwrap();

    auto small_value   = rstd::as<rstd::alloc::Allocator>(allocator).allocate(small).unwrap();
    auto aligned_value = rstd::as<rstd::alloc::Allocator>(allocator).allocate(aligned).unwrap();
    auto large_value   = rstd::as<rstd::alloc::Allocator>(allocator).allocate(large).unwrap();
    rstd::as<rstd::alloc::Allocator>(allocator).deallocate(small_value.pointer, small);
    rstd::as<rstd::alloc::Allocator>(allocator).deallocate(aligned_value.pointer, aligned);
    rstd::as<rstd::alloc::Allocator>(allocator).deallocate(large_value.pointer, large);

    EXPECT_EQ(arena.stats().layout_classes, usize(3));
    EXPECT_GE(arena.stats().recycled_capacity, usize(3));
    EXPECT_EQ(rstd::as<rstd::alloc::Allocator>(allocator).allocate(aligned).unwrap().pointer,
              aligned_value.pointer);
    EXPECT_EQ(rstd::as<rstd::alloc::Allocator>(allocator).allocate(small).unwrap().pointer,
              small_value.pointer);
    EXPECT_EQ(rstd::as<rstd::alloc::Allocator>(allocator).allocate(large).unwrap().pointer,
              large_value.pointer);
}

TEST(RecyclingArena, VecGrowthMakesOldBuffersReusable) {
    ArenaUpstreamState upstream {};
    {
        alloc::RecyclingArena arena(usize(1024), ArenaTestUpstream { &upstream });
        using Allocator = decltype(arena.allocator());
        auto values =
            alloc::vec::Vec<int, Allocator>::with_capacity_in(usize(1), arena.allocator());
        values.push(1);
        values.push(2);
        values.push(3);

        auto reused =
            alloc::vec::Vec<int, Allocator>::with_capacity_in(usize(1), arena.allocator());
        reused.push(4);
        EXPECT_GE(arena.stats().reuses, usize(1));
        EXPECT_EQ(reused[usize {}], 4);
    }
    EXPECT_EQ(upstream.allocations, upstream.deallocations);
}

TEST(RecyclingArena, SupportsRuntimeAllocatorReferences) {
    alloc::RecyclingArena arena(usize(1024));
    auto                  concrete  = arena.allocator();
    auto                  allocator = rstd::alloc::allocator_ref(concrete);
    using Allocator                 = ref<dyn<rstd::alloc::Allocator>>;

    static_assert(rstd::Impled<Allocator, rstd::alloc::Allocator>);
    {
        auto values = alloc::vec::Vec<int, Allocator>::with_capacity_in(usize(1), allocator);
        for (auto value = 0; value < 32; ++value) values.emplace_back(value);
        EXPECT_EQ(values.len(), usize(32));
        EXPECT_EQ(values[usize(17)], 17);
        EXPECT_GT(arena.stats().live_bytes, usize {});
    }
    EXPECT_EQ(arena.stats().live_bytes, usize {});
}

TEST(RecyclingArena, DeallocationDoesNotGrowMetadata) {
    alloc::RecyclingArena arena(usize(4096));
    auto                  allocator = arena.allocator();
    auto const layout = rstd::alloc::Layout::from_size_align(usize(32), usize(8)).unwrap();
    auto       values = alloc::vec::Vec<rstd::alloc::Allocation>();

    for (auto index = usize {}; index < usize(64); ++index) {
        values.push(rstd::as<rstd::alloc::Allocator>(allocator).allocate(layout).unwrap());
    }
    auto capacity          = arena.stats().recycled_capacity;
    auto metadata_reserved = arena.stats().metadata_reserved_bytes;
    for (auto value : values) {
        rstd::as<rstd::alloc::Allocator>(allocator).deallocate(value.pointer, layout);
    }

    EXPECT_EQ(arena.stats().live_bytes, usize {});
    EXPECT_EQ(arena.stats().free_bytes, usize(64 * 32));
    EXPECT_EQ(arena.stats().layout_classes, usize(1));
    EXPECT_EQ(arena.stats().recycled_capacity, capacity);
    EXPECT_EQ(arena.stats().metadata_reserved_bytes, metadata_reserved);
}

TEST(RecyclingArena, UnpublishedSlabsAreReleasedWhenMetadataFails) {
    const int sizes[] { 32, 700 };
    for (auto fail_at = 1; fail_at <= 2; ++fail_at) {
        for (auto size : sizes) {
            ArenaUpstreamState upstream {};
            upstream.fail_at = fail_at;
            {
                alloc::RecyclingArena arena(usize(1024), ArenaTestUpstream { &upstream });
                auto layout = rstd::alloc::Layout::from_size_align(usize(size), usize(16)).unwrap();
                EXPECT_TRUE(arena.allocate(layout).is_err());
                EXPECT_EQ(arena.stats().live_bytes, usize());
                EXPECT_EQ(arena.stats().allocations, usize());
                EXPECT_EQ(arena.stats().layout_classes, usize());
                EXPECT_EQ(arena.stats().reserved_bytes, usize());
                EXPECT_EQ(upstream.live_bytes, arena.stats().metadata_reserved_bytes);
                upstream.fail_at = 0;
                auto allocation  = arena.allocate(layout).unwrap();
                EXPECT_EQ(arena.stats().reserved_bytes + arena.stats().metadata_reserved_bytes,
                          upstream.live_bytes);
                auto attempts    = upstream.attempts;
                upstream.fail_at = attempts + 1;
                arena.deallocate(allocation.pointer, layout);
                EXPECT_EQ(upstream.attempts, attempts);
                EXPECT_EQ(arena.allocate(layout).unwrap().pointer, allocation.pointer);
                EXPECT_EQ(upstream.attempts, attempts);
                arena.deallocate(allocation.pointer, layout);
            }
            EXPECT_EQ(upstream.live_bytes, usize());
            EXPECT_EQ(upstream.allocations, upstream.deallocations);
            EXPECT_FALSE(upstream.release_layout_mismatch);
        }
    }
}

TEST(RecyclingArena, PointerCapacityFailureDoesNotConsumePayloadCursor) {
    ArenaUpstreamState upstream {};
    {
        alloc::RecyclingArena arena(usize(64 * 1024), ArenaTestUpstream { &upstream });
        auto  layout = rstd::alloc::Layout::from_size_align(usize(1), usize(1)).unwrap();
        void* pointers[8192] {};
        pointers[0]      = arena.allocate(layout).unwrap().pointer;
        auto count       = usize(1);
        upstream.fail_at = upstream.attempts + 1;
        bool failed      = false;
        for (; count < usize(8192); ++count) {
            auto before     = arena.stats();
            auto allocation = arena.allocate(layout);
            if (allocation.is_err()) {
                auto after = arena.stats();
                EXPECT_EQ(after.live_bytes, before.live_bytes);
                EXPECT_EQ(after.allocations, before.allocations);
                EXPECT_EQ(after.free_bytes, before.free_bytes);
                EXPECT_EQ(after.recycled_capacity, before.recycled_capacity);
                EXPECT_EQ(after.reserved_bytes, before.reserved_bytes);
                EXPECT_GE(after.metadata_reserved_bytes, before.metadata_reserved_bytes);
                failed = true;
                break;
            }
            pointers[count.to_primitive()] = allocation->pointer;
        }
        ASSERT_TRUE(failed);
        auto next = arena.allocate(layout).unwrap();
        EXPECT_EQ(next.pointer, static_cast<rstd::byte*>(pointers[0]) + count.to_primitive());
        arena.deallocate(next.pointer, layout);
        for (auto index = usize(); index < count; ++index)
            arena.deallocate(pointers[index.to_primitive()], layout);
        EXPECT_EQ(arena.stats().live_bytes, usize());
        EXPECT_EQ(arena.stats().reserved_bytes + arena.stats().metadata_reserved_bytes,
                  upstream.live_bytes);
    }
    EXPECT_EQ(upstream.live_bytes, usize());
    EXPECT_FALSE(upstream.release_layout_mismatch);
}

TEST(RecyclingArena, LayoutClassFailureDoesNotPublishClassOrAllocation) {
    ArenaUpstreamState upstream {};
    {
        alloc::RecyclingArena arena(usize(1024 * 1024), ArenaTestUpstream { &upstream });
        auto  first_layout = rstd::alloc::Layout::from_size_align(usize(1), usize(1)).unwrap();
        auto* first = static_cast<rstd::byte*>(arena.allocate(first_layout).unwrap().pointer);
        usize used(1);
        upstream.fail_at = upstream.attempts + 1;
        bool failed      = false;
        for (auto size = usize(2); size < usize(1024); ++size) {
            auto layout     = rstd::alloc::Layout::from_size_align(size, usize(1)).unwrap();
            auto before     = arena.stats();
            auto allocation = arena.allocate(layout);
            if (allocation.is_err()) {
                auto after = arena.stats();
                EXPECT_EQ(after.layout_classes, before.layout_classes);
                EXPECT_EQ(after.live_bytes, before.live_bytes);
                EXPECT_EQ(after.allocations, before.allocations);
                EXPECT_EQ(after.recycled_capacity, before.recycled_capacity);
                EXPECT_EQ(after.reserved_bytes, before.reserved_bytes);
                EXPECT_GE(after.metadata_used_bytes, before.metadata_used_bytes);
                auto retry = arena.allocate(layout).unwrap();
                EXPECT_EQ(retry.pointer, first + used.to_primitive());
                failed = true;
                break;
            }
            used += size;
        }
        EXPECT_TRUE(failed);
        EXPECT_EQ(arena.stats().reserved_bytes + arena.stats().metadata_reserved_bytes,
                  upstream.live_bytes);
    }
    EXPECT_EQ(upstream.live_bytes, usize());
    EXPECT_FALSE(upstream.release_layout_mismatch);
}

TEST(RecyclingArena, OwnsRcControlBlocksThroughRstdAllocator) {
    alloc::RecyclingArena arena(usize(1024));
    auto                  drops = 0;
    {
        auto value = alloc::rc::make_rc_in<ArenaRcProbe>(arena.allocator(), drops);
        auto clone = value.clone();
        EXPECT_EQ(clone->drops, &drops);
        EXPECT_GT(arena.stats().live_bytes, usize {});
    }
    EXPECT_EQ(drops, 1);
    EXPECT_EQ(arena.stats().live_bytes, usize {});
}

TEST(RecyclingArena, OwnsHashMapBucketsThroughRstdAllocator) {
    alloc::RecyclingArena arena(usize(1024));
    using Allocator = decltype(arena.allocator());
    using Map       = rstd::collections::HashMap<int,
                                                 int,
                                                 rstd::hash::RandomState,
                                                 ::alloc::collections::DefaultHashEqual<int>,
                                                 Allocator>;
    {
        auto values = Map::make_in(arena.allocator());
        for (auto value = 0; value < 32; ++value) values.insert(value, value * 2);
        EXPECT_EQ(values.len(), usize(32));
        EXPECT_EQ(**values.get(17), 34);
        EXPECT_GT(arena.stats().live_bytes, usize {});
    }
    EXPECT_EQ(arena.stats().live_bytes, usize {});
}

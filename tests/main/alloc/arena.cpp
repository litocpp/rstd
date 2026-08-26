#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;

struct ArenaUpstreamState {
    int allocations;
    int deallocations;
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
        ++this->self().state->allocations;
        return as<alloc::Allocator>(::alloc::GLOBAL).allocate(layout);
    }

    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        ++this->self().state->deallocations;
        as<alloc::Allocator>(::alloc::GLOBAL).deallocate(pointer, layout);
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
        EXPECT_EQ(stats.reserved_bytes, usize(2648));
        EXPECT_EQ(stats.peak_used_bytes, stats.used_bytes);
    }
    EXPECT_EQ(upstream.allocations, 3);
    EXPECT_EQ(upstream.deallocations, 3);
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
        EXPECT_EQ(upstream.allocations, 1);
        rstd::as<rstd::alloc::Allocator>(allocator).deallocate(second.pointer, layout);
        EXPECT_EQ(arena.stats().live_bytes, usize());
    }
    EXPECT_EQ(upstream.allocations, upstream.deallocations);
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

TEST(RecyclingArena, DeallocationDoesNotGrowMetadata) {
    alloc::RecyclingArena arena(usize(4096));
    auto                  allocator = arena.allocator();
    auto const layout = rstd::alloc::Layout::from_size_align(usize(32), usize(8)).unwrap();
    auto       values = alloc::vec::Vec<rstd::alloc::Allocation>();

    for (auto index = usize {}; index < usize(64); ++index) {
        values.push(rstd::as<rstd::alloc::Allocator>(allocator).allocate(layout).unwrap());
    }
    for (auto value : values) {
        rstd::as<rstd::alloc::Allocator>(allocator).deallocate(value.pointer, layout);
    }

    EXPECT_EQ(arena.stats().live_bytes, usize {});
    EXPECT_EQ(arena.stats().free_bytes, usize(64 * 32));
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
        auto values = Map::new_in(arena.allocator());
        for (auto value = 0; value < 32; ++value) values.insert(value, value * 2);
        EXPECT_EQ(values.len(), usize(32));
        EXPECT_EQ(**values.get(17), 34);
        EXPECT_GT(arena.stats().live_bytes, usize {});
    }
    EXPECT_EQ(arena.stats().live_bytes, usize {});
}

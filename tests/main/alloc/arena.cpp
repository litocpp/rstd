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

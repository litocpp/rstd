#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;
using rstd::alloc::Allocator;
using rstd::alloc::Layout;

struct SharedArenaUpstreamState {
    usize allocations;
    usize deallocations;
    usize bytes;
};

struct SharedArenaTestUpstream {
    SharedArenaUpstreamState* state;

    auto statistics() const -> SharedArenaUpstreamState { return *state; }
};

namespace rstd
{

template<>
struct Impl<alloc::Allocator, ::SharedArenaTestUpstream>
    : DefaultInImpl<alloc::Allocator, ::SharedArenaTestUpstream> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        ++this->self().state->allocations;
        this->self().state->bytes += layout.size;
        return as<alloc::Allocator>(::alloc::GLOBAL).allocate(layout);
    }

    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        ++this->self().state->deallocations;
        this->self().state->bytes -= layout.size;
        as<alloc::Allocator>(::alloc::GLOBAL).deallocate(pointer, layout);
    }
};

} // namespace rstd

TEST(SharedArena, LastStrongHandleReleasesAllSlabs) {
    using Root   = rstd::alloc::SharedBumpArena<SharedArenaTestUpstream>;
    using Handle = rstd::alloc::SharedArenaAllocator<SharedArenaTestUpstream>;
    using Weak   = rstd::alloc::WeakSharedArena<SharedArenaTestUpstream>;

    auto state     = SharedArenaUpstreamState {};
    auto allocator = Option<Handle> {};
    auto weak      = Option<Weak> {};
    {
        auto root   = Root::make(usize(1024), SharedArenaTestUpstream { &state });
        auto first  = root.allocator();
        auto second = first.clone();
        auto layout = Layout::array<u8>(usize(400)).unwrap();

        EXPECT_TRUE(rstd::as<Allocator>(first).allocate(layout).is_ok());
        EXPECT_TRUE(rstd::as<Allocator>(first).allocate(layout).is_ok());
        EXPECT_TRUE(rstd::as<Allocator>(first).allocate(layout).is_ok());
        auto statistics = root.statistics();
        EXPECT_EQ(statistics.arena.ordinary_slabs, usize(2));
        EXPECT_EQ(statistics.upstream.allocations, usize(2));
        EXPECT_EQ(statistics.upstream.bytes, usize(2048));
        allocator = Some(rstd::move(second));
        weak      = Some(root.downgrade());
    }

    EXPECT_FALSE(weak->expired());
    auto layout = Layout::array<u8>(usize(400)).unwrap();
    EXPECT_TRUE(rstd::as<Allocator>(*allocator).allocate(layout).is_ok());
    allocator = None();
    EXPECT_TRUE(weak->expired());
    EXPECT_EQ(state.allocations, state.deallocations);
    EXPECT_EQ(state.bytes, usize());
}

TEST(SharedArena, SupportsAllocatorAwareVecAndNestedArena) {
    auto state = SharedArenaUpstreamState {};
    auto root  = rstd::alloc::SharedBumpArena<SharedArenaTestUpstream>::make(
        usize(4096), SharedArenaTestUpstream { &state });
    auto values = alloc::vec::Vec<int, decltype(root.allocator())>::with_capacity_in(
        usize(1), root.allocator());
    values.push(1);
    values.push(2);

    auto nested           = ::alloc::BumpArena(usize(1024), root.allocator());
    auto layout           = Layout::array<u8>(usize(128)).unwrap();
    auto nested_allocator = nested.allocator();
    EXPECT_TRUE(rstd::as<Allocator>(nested_allocator).allocate(layout).is_ok());
    EXPECT_EQ(values.len(), usize(2));
    EXPECT_GT(root.arena_statistics().used_bytes, usize());
}

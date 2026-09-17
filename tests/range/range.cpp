#include <rstd/test/gtest.hpp>
import rstd;
using namespace rstd::prelude;

struct RangeFailState {
    bool fail {};
    int  calls {};
};
struct RangeMetadata {
    RangeFailState* state;
};
namespace rstd
{
template<>
struct Impl<alloc::Allocator, ::RangeMetadata> : DefaultInImpl<alloc::Allocator, ::RangeMetadata> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        ++this->self().state->calls;
        if (this->self().state->fail) return Err(alloc::AllocError {});
        return as<alloc::Allocator>(::alloc::GLOBAL).allocate(layout);
    }
    void deallocate(void* p, alloc::Layout l) const noexcept {
        as<alloc::Allocator>(::alloc::GLOBAL).deallocate(p, l);
    }
};
} // namespace rstd
TEST(Range, ModelAndCoalescing) {
    alloc::RangeAllocator                   ranges(4096);
    alloc::vec::Vec<alloc::RangeAllocation> live;
    bool                                    occupied[4096] {};
    rstd::uint32_t                          random = 2342435;
    for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        random = random * 1664525U + 1013904223U;
        if (! live.is_empty() && random % 3 == 0) {
            auto index = usize(random % live.len().to_primitive());
            auto value = live.remove(index);
            for (auto i = value.offset; i < value.offset + value.size; ++i) {
                EXPECT_TRUE(occupied[i]);
                occupied[i] = false;
            }
            EXPECT_TRUE(ranges.deallocate(value.id).is_ok());
            EXPECT_TRUE(ranges.deallocate(value.id).is_err());
        } else {
            auto size      = 1U + random % 79;
            auto alignment = 1U << ((random >> 9) % 7);
            bool possible  = false;
            for (unsigned offset = 0; offset + size <= 4096; offset += alignment) {
                bool free = true;
                for (unsigned i = offset; i < offset + size; ++i) free &= ! occupied[i];
                possible |= free;
            }
            auto result = ranges.allocate(size, alignment);
            EXPECT_EQ(result.is_ok(), possible);
            if (result.is_ok()) {
                auto value = result.unwrap_unchecked();
                EXPECT_EQ(value.offset % alignment, 0u);
                for (auto i = value.offset; i < value.offset + value.size; ++i) {
                    EXPECT_FALSE(occupied[i]);
                    occupied[i] = true;
                }
                live.push(rstd::move(value));
            }
        }
        alloc::RangeSize expected_bytes = 0;
        for (const auto& v : live) expected_bytes += v.size;
        const auto counts   = ranges.counters();
        const auto detailed = ranges.statistics();
        EXPECT_EQ(counts.allocation_count, live.len().to_primitive());
        EXPECT_EQ(counts.requested_bytes, expected_bytes);
        EXPECT_EQ(counts.occupied_bytes, expected_bytes);
        EXPECT_EQ(counts.padding_bytes, 0u);
        EXPECT_EQ(counts.free_bytes, 4096 - expected_bytes);
        EXPECT_EQ(counts.capacity, detailed.capacity);
        EXPECT_EQ(counts.allocation_count, detailed.allocation_count);
        EXPECT_EQ(counts.requested_bytes, detailed.requested_bytes);
        EXPECT_EQ(counts.occupied_bytes, detailed.occupied_bytes);
        EXPECT_EQ(counts.free_bytes, detailed.free_bytes);
        EXPECT_EQ(counts.metadata_bytes, detailed.metadata_bytes);
        EXPECT_EQ(counts.search_steps, detailed.search_steps);
    }
    for (auto& v : live) EXPECT_TRUE(ranges.deallocate(v.id).is_ok());
    EXPECT_EQ(ranges.statistics().largest_free_range, 4096u);
    EXPECT_TRUE(ranges.allocate(4096, 1).is_ok());
}
TEST(Range, IdentityAndLargeBoundaries) {
    alloc::RangeAllocator a(~alloc::RangeSize(0)), b(4096);
    EXPECT_TRUE(a.allocate(0, 1).is_err());
    EXPECT_TRUE(a.allocate(1, 3).is_err());
    auto first = a.allocate(alloc::RangeSize(1) << 40, 4096).unwrap_unchecked();
    EXPECT_TRUE(b.deallocate(first.id).is_err());
    a.reset();
    auto second = a.allocate(16, 16).unwrap_unchecked();
    EXPECT_TRUE(a.deallocate(first.id).is_err());
    EXPECT_TRUE(a.get(second.id).is_some());
    a.reset();
    auto full = a.allocate(~alloc::RangeSize(0), 1).unwrap_unchecked();
    EXPECT_TRUE(a.allocate(1, 1).is_err());
    EXPECT_TRUE(a.deallocate(full.id).is_ok());
    alloc::BumpRangeAllocator linear(~alloc::RangeSize(0));
    auto                      old = linear.allocate(~alloc::RangeSize(0) - 1, 1).unwrap_unchecked();
    EXPECT_TRUE(linear.allocate(1, 8).is_err());
    EXPECT_TRUE(linear.contains(old));
    linear.reset();
    EXPECT_FALSE(linear.contains(old));
}
TEST(Range, MetadataFailureDoesNotDamageAllocations) {
    RangeFailState        state { true };
    alloc::RangeAllocator ranges(4096, RangeMetadata { &state });
    EXPECT_TRUE(ranges.allocate(8, 1).is_err());
    EXPECT_EQ(ranges.statistics().free_bytes, 4096u);
    state.fail = false;
    alloc::vec::Vec<alloc::RangeAllocation> live;
    bool                                    failed = false;
    for (unsigned i = 0; i < 100; ++i) {
        auto before = ranges.counters();
        auto result = ranges.allocate(3, 8);
        if (result.is_err()) {
            EXPECT_EQ(ranges.counters().allocation_count, before.allocation_count);
            EXPECT_EQ(ranges.counters().requested_bytes, before.requested_bytes);
            EXPECT_EQ(ranges.counters().free_bytes, before.free_bytes);
            failed = true;
            break;
        }
        live.push(result.unwrap_unchecked());
        state.fail = true;
    }
    EXPECT_TRUE(failed);
    auto calls = state.calls;
    for (auto& value : live) {
        EXPECT_TRUE(ranges.get(value.id).is_some());
        EXPECT_TRUE(ranges.deallocate(value.id).is_ok());
    }
    EXPECT_EQ(calls, state.calls);
    EXPECT_EQ(ranges.statistics().largest_free_range, 4096u);
}
TEST(Range, CpuArenaAndContainers) {
    alloc::RangeArena arena(
        rstd::alloc::Layout::from_size_align(usize(65536), usize(256)).unwrap());
    {
        auto allocator = arena.allocator();
        auto vec       = alloc::vec::Vec<int, decltype(allocator)>::new_in(allocator);
        for (int i = 0; i < 1000; ++i) vec.push(int(i));
        for (int i = 0; i < 1000; ++i) EXPECT_EQ(vec[usize(i)], i);
        auto layout = rstd::alloc::Layout::from_size_align(usize(64), usize(256)).unwrap();
        auto mem    = rstd::as<rstd::alloc::Allocator>(allocator).allocate(layout).unwrap();
        EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(mem.pointer) % 256, 0u);
        arena.deallocate(mem.pointer, layout);
    }
    EXPECT_EQ(arena.statistics().allocation_count, 0u);
    EXPECT_EQ(arena.statistics().largest_free_range, 65536u);
}
TEST(Range, GrowZeroedPreservesDataAndReleasesOldRange) {
    alloc::RangeArena arena(rstd::alloc::Layout::from_size_align(usize(4096), usize(64)).unwrap());
    auto              allocator = arena.allocator();
    auto              small = rstd::alloc::Layout::from_size_align(usize(16), usize(8)).unwrap();
    auto              large = rstd::alloc::Layout::from_size_align(usize(64), usize(8)).unwrap();
    auto  allocation        = rstd::as<rstd::alloc::Allocator>(allocator).allocate(small).unwrap();
    auto* bytes             = static_cast<rstd::uint8_t*>(allocation.pointer);
    for (unsigned i = 0; i < 16; ++i) bytes[i] = rstd::uint8_t(i + 1);
    auto grown = rstd::as<rstd::alloc::Allocator>(allocator)
                     .grow_zeroed(allocation.pointer, small, large)
                     .unwrap();
    bytes      = static_cast<rstd::uint8_t*>(grown.pointer);
    for (unsigned i = 0; i < 64; ++i) EXPECT_EQ(bytes[i], i < 16 ? i + 1 : 0);
    EXPECT_EQ(arena.statistics().allocation_count, 1u);
    arena.deallocate(grown.pointer, large);
    EXPECT_EQ(arena.statistics().allocation_count, 0u);
}
TEST(Range, CpuMetadataFailureAndFailedGrowth) {
    RangeFailState    state;
    alloc::RangeArena arena(rstd::alloc::Layout::from_size_align(usize(256), usize(64)).unwrap(),
                            alloc::Global {},
                            RangeMetadata { &state });
    auto              allocator = arena.allocator();
    auto              small = rstd::alloc::Layout::from_size_align(usize(16), usize(8)).unwrap();
    auto              large = rstd::alloc::Layout::from_size_align(usize(1024), usize(8)).unwrap();
    state.fail              = true;
    EXPECT_TRUE(arena.allocate(small).is_err());
    state.fail                                       = false;
    auto allocation                                  = arena.allocate(small).unwrap();
    *static_cast<unsigned char*>(allocation.pointer) = 42;
    EXPECT_TRUE(rstd::as<rstd::alloc::Allocator>(allocator)
                    .grow_zeroed(allocation.pointer, small, large)
                    .is_err());
    EXPECT_EQ(*static_cast<unsigned char*>(allocation.pointer), 42u);
    EXPECT_EQ(arena.statistics().allocation_count, 1u);
    auto calls = state.calls;
    state.fail = true;
    arena.deallocate(allocation.pointer, small);
    EXPECT_EQ(calls, state.calls);
    EXPECT_EQ(arena.statistics().allocation_count, 0u);
}

TEST(Range, CountersResetAndFailureAtomicity) {
    RangeFailState        state;
    alloc::RangeAllocator ranges(4096, RangeMetadata { &state });
    EXPECT_EQ(ranges.counters().free_bytes, 4096u);
    auto first     = ranges.allocate(13, 16).unwrap_unchecked();
    auto before    = ranges.counters();
    state.fail     = true;
    auto oversized = ranges.allocate(4096, 1);
    EXPECT_TRUE(oversized.is_err());
    EXPECT_TRUE(ranges.deallocate(alloc::RangeId {}).is_err());
    EXPECT_EQ(ranges.counters().requested_bytes, before.requested_bytes);
    EXPECT_EQ(ranges.counters().allocation_count, before.allocation_count);
    auto calls = state.calls;
    for (unsigned i = 0; i < 100; ++i) EXPECT_EQ(ranges.counters().allocation_count, 1u);
    EXPECT_TRUE(ranges.deallocate(first.id).is_ok());
    EXPECT_EQ(calls, state.calls);
    EXPECT_EQ(ranges.counters().free_bytes, 4096u);
    state.fail       = false;
    auto live        = ranges.allocate(31, 8).unwrap_unchecked();
    auto retained    = ranges.counters().metadata_bytes;
    auto reset_calls = state.calls;
    state.fail       = true;
    ranges.reset();
    EXPECT_EQ(state.calls, reset_calls);
    EXPECT_FALSE(ranges.get(live.id).is_some());
    EXPECT_EQ(ranges.counters().allocation_count, 0u);
    EXPECT_EQ(ranges.counters().free_bytes, 4096u);
    EXPECT_EQ(ranges.counters().metadata_bytes, retained);
    EXPECT_FALSE(ranges.get(first.id).is_some());
}

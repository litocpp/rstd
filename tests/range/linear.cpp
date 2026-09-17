#include <rstd/test/gtest.hpp>
import rstd;
using namespace rstd::prelude;

static_assert(__is_same(alloc::LinearRangeAllocator, alloc::BumpRangeAllocator));

struct LinearFailure {
    int remaining { -1 }, calls {}, live {};
};
struct LinearMetadata {
    LinearFailure* state;
};
namespace rstd
{
template<>
struct Impl<alloc::Allocator, ::LinearMetadata>
    : DefaultInImpl<alloc::Allocator, ::LinearMetadata> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        auto& state = *this->self().state;
        ++state.calls;
        if (state.remaining == 0) return Err(alloc::AllocError {});
        if (state.remaining > 0) --state.remaining;
        auto result = as<alloc::Allocator>(::alloc::GLOBAL).allocate(layout);
        if (result.is_ok()) ++state.live;
        return result;
    }
    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        --this->self().state->live;
        as<alloc::Allocator>(::alloc::GLOBAL).deallocate(pointer, layout);
    }
};
} // namespace rstd

TEST(StackRange, OrderIdentityAndPadding) {
    alloc::StackRangeAllocator ranges(64), other(64);
    EXPECT_TRUE(ranges.top().is_none());
    auto a = ranges.allocate(3, 1).unwrap_unchecked();
    auto b = ranges.allocate(7, 16).unwrap_unchecked();
    EXPECT_EQ(b.offset, 16u);
    auto stats = ranges.statistics();
    EXPECT_EQ(stats.requested_bytes, 10u);
    EXPECT_EQ(stats.occupied_bytes, 23u);
    EXPECT_EQ(stats.padding_bytes, 13u);
    EXPECT_EQ(ranges.pop(a.id).unwrap_err_unchecked(), alloc::RangeError::OutOfOrder);
    EXPECT_EQ(other.pop(b.id).unwrap_err_unchecked(), alloc::RangeError::InvalidAllocation);
    EXPECT_TRUE(ranges.pop(b.id).is_ok());
    EXPECT_EQ(ranges.statistics().occupied_bytes, 3u);
    auto replacement = ranges.allocate(7, 16).unwrap_unchecked();
    EXPECT_EQ(replacement.offset, b.offset);
    EXPECT_FALSE(replacement.id == b.id);
    EXPECT_TRUE(ranges.get(b.id).is_none());
    EXPECT_TRUE(ranges.pop(b.id).is_err());
    EXPECT_TRUE(ranges.get(a.id).is_some());
    ranges.reset();
    auto current = ranges.allocate(3, 1).unwrap_unchecked();
    EXPECT_TRUE(ranges.pop(a.id).is_err());
    EXPECT_TRUE(ranges.get(replacement.id).is_none());
    EXPECT_TRUE(ranges.pop(current.id).is_ok());
    EXPECT_TRUE(ranges.top().is_none());
    EXPECT_EQ(ranges.statistics().largest_free_range, 64u);
}

TEST(StackRange, ModelAndCpuStorage) {
    constexpr unsigned capacity = 4096;
    alloc::BumpArena   storage;
    auto  layout  = rstd::alloc::Layout::from_size_align(usize(capacity), usize(256)).unwrap();
    auto  backing = storage.allocate(layout).unwrap();
    auto* bytes   = static_cast<unsigned char*>(backing.pointer);
    alloc::StackRangeAllocator ranges(capacity);
    struct Live {
        alloc::StackRangeAllocation range;
        unsigned                    before;
        unsigned char               pattern;
    };
    alloc::vec::Vec<Live> live;
    unsigned              cursor = 0, requested = 0;
    rstd::uint32_t        random = 0x198762;
    for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        if (! live.is_empty() && random % 2 == 0) {
            auto value = live.pop().unwrap_unchecked();
            for (auto i = value.range.offset; i < value.range.offset + value.range.size; ++i)
                EXPECT_EQ(bytes[i], value.pattern);
            ASSERT_TRUE(ranges.pop(value.range.id).is_ok());
            cursor = value.before;
            requested -= unsigned(value.range.size);
        } else {
            unsigned size = 1 + (random >> 7) % 111, alignment = 1U << ((random >> 20) % 9);
            unsigned offset = (cursor + alignment - 1) / alignment * alignment;
            auto     result = ranges.allocate(size, alignment);
            ASSERT_EQ(result.is_ok(), offset + size <= capacity);
            if (result.is_ok()) {
                auto range = result.unwrap_unchecked();
                EXPECT_EQ(range.offset, offset);
                EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(bytes + offset) % alignment, 0u);
                auto pattern = static_cast<unsigned char>(iteration);
                for (unsigned i = offset; i < offset + size; ++i) bytes[i] = pattern;
                live.push(Live { range, cursor, pattern });
                cursor = offset + size;
                requested += size;
            }
        }
        auto stats = ranges.statistics();
        EXPECT_EQ(stats.occupied_bytes, cursor);
        EXPECT_EQ(stats.requested_bytes, requested);
    }
    while (! live.is_empty()) {
        auto value = live.pop().unwrap_unchecked();
        for (auto i = value.range.offset; i < value.range.offset + value.range.size; ++i)
            EXPECT_EQ(bytes[i], value.pattern);
        ASSERT_TRUE(ranges.pop(value.range.id).is_ok());
    }
    EXPECT_EQ(ranges.statistics().free_bytes, capacity);
}

TEST(RingRange, WrapOrderAndIdentity) {
    alloc::RingRangeAllocator ranges(100), other(100);
    auto                      a = ranges.allocate(30, 1).unwrap_unchecked();
    auto                      b = ranges.allocate(30, 1).unwrap_unchecked();
    auto                      c = ranges.allocate(30, 1).unwrap_unchecked();
    EXPECT_EQ(ranges.release(b.id).unwrap_err_unchecked(), alloc::RangeError::OutOfOrder);
    EXPECT_TRUE(other.release(a.id).is_err());
    EXPECT_TRUE(ranges.release(a.id).is_ok());
    auto d = ranges.allocate(20, 16).unwrap_unchecked();
    EXPECT_EQ(d.offset, 0u);
    auto stats = ranges.statistics();
    EXPECT_EQ(stats.occupied_bytes, 90u);
    EXPECT_EQ(stats.requested_bytes, 80u);
    EXPECT_EQ(stats.padding_bytes, 10u);
    EXPECT_EQ(stats.largest_free_range, 10u);
    EXPECT_TRUE(ranges.allocate(11, 1).is_err());
    EXPECT_TRUE(ranges.get(a.id).is_none());
    EXPECT_TRUE(ranges.release(a.id).is_err());
    EXPECT_TRUE(ranges.release(b.id).is_ok());
    EXPECT_TRUE(ranges.release(c.id).is_ok());
    EXPECT_EQ(ranges.statistics().occupied_bytes, 30u);
    EXPECT_EQ(ranges.statistics().largest_free_range, 70u);
    EXPECT_TRUE(ranges.release(d.id).is_ok());
    EXPECT_TRUE(ranges.front().is_none());
    auto full = ranges.allocate(100, 1).unwrap_unchecked();
    EXPECT_EQ(ranges.statistics().free_bytes, 0u);
    EXPECT_TRUE(ranges.allocate(1, 1).is_err());
    ranges.reset();
    auto again = ranges.allocate(100, 1).unwrap_unchecked();
    EXPECT_FALSE(full.id == again.id);
    EXPECT_TRUE(ranges.release(full.id).is_err());
    EXPECT_TRUE(ranges.release(again.id).is_ok());
    EXPECT_EQ(ranges.statistics().free_bytes, 100u);
}

TEST(RingRange, ModelAndCpuStorage) {
    constexpr unsigned capacity = 257;
    alloc::BumpArena   storage;
    auto               backing =
        storage.allocate(rstd::alloc::Layout::from_size_align(usize(capacity), usize(64)).unwrap())
            .unwrap();
    auto*                     bytes = static_cast<unsigned char*>(backing.pointer);
    alloc::RingRangeAllocator ranges(capacity);
    struct Live {
        alloc::RingRangeAllocation range;
        alloc::RangeSize           end;
        unsigned char              pattern;
    };
    alloc::vec::Vec<Live> live;
    alloc::RangeSize      read = 0, write = 0, requested = 0;
    rstd::uint32_t        random = 0x975ea1;
    for (unsigned iteration = 0; iteration < 20000; ++iteration) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        if (! live.is_empty() && random % 3 == 0) {
            auto value = live.remove(usize(0));
            for (auto i = value.range.offset; i < value.range.offset + value.range.size; ++i)
                EXPECT_EQ(bytes[i], value.pattern);
            ASSERT_TRUE(ranges.release(value.range.id).is_ok());
            read = value.end;
            requested -= value.range.size;
            if (live.is_empty()) read = write = 0;
        } else {
            const unsigned size = 1 + (random >> 7) % 71, alignment = 1U << ((random >> 22) % 7);
            auto           lap    = write / capacity,
                           offset = ((write % capacity + alignment - 1) / alignment) * alignment;
            if (offset + size > capacity) {
                ++lap;
                offset = 0;
            }
            auto end    = lap * capacity + offset + size;
            auto result = ranges.allocate(size, alignment);
            ASSERT_EQ(result.is_ok(), end - read <= capacity);
            if (result.is_ok()) {
                auto value = result.unwrap_unchecked();
                EXPECT_EQ(value.offset, offset);
                EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(bytes + offset) % alignment, 0u);
                for (const auto& old : live)
                    EXPECT_TRUE(offset + size <= old.range.offset ||
                                old.range.offset + old.range.size <= offset);
                auto pattern = static_cast<unsigned char>(iteration);
                for (auto i = offset; i < offset + size; ++i) bytes[i] = pattern;
                live.push(Live { value, end, pattern });
                write = end;
                requested += size;
            }
        }
        auto stats = ranges.statistics();
        EXPECT_EQ(stats.occupied_bytes, write - read);
        EXPECT_EQ(stats.requested_bytes, requested);
        EXPECT_EQ(stats.free_bytes, capacity - (write - read));
        if (! live.is_empty()) EXPECT_EQ(ranges.front()->id, live[usize(0)].range.id);
    }
    for (const auto& value : live) {
        for (auto i = value.range.offset; i < value.range.offset + value.range.size; ++i)
            EXPECT_EQ(bytes[i], value.pattern);
        ASSERT_TRUE(ranges.release(value.range.id).is_ok());
    }
    EXPECT_EQ(ranges.statistics().free_bytes, capacity);
}

TEST(LinearRange, InvalidLayoutsAndLargeBoundaries) {
    constexpr auto             maximum = ~alloc::RangeSize(0);
    alloc::StackRangeAllocator stack(maximum);
    alloc::RingRangeAllocator  ring(maximum);
    EXPECT_TRUE(stack.allocate(0, 1).is_err());
    EXPECT_TRUE(ring.allocate(0, 1).is_err());
    EXPECT_TRUE(stack.allocate(1, 0).is_err());
    EXPECT_TRUE(ring.allocate(1, 3).is_err());
    auto s = stack.allocate(maximum - 7, 1).unwrap_unchecked();
    EXPECT_TRUE(stack.allocate(8, 8).is_err());
    EXPECT_TRUE(stack.pop(s.id).is_ok());
    auto full = stack.allocate(maximum, 1).unwrap_unchecked();
    EXPECT_TRUE(stack.allocate(1, 1).is_err());
    EXPECT_TRUE(stack.pop(full.id).is_ok());
    auto a = ring.allocate(maximum - 7, 1).unwrap_unchecked();
    auto b = ring.allocate(3, 1).unwrap_unchecked();
    EXPECT_TRUE(ring.release(a.id).is_ok());
    auto c = ring.allocate(8, 8).unwrap_unchecked();
    EXPECT_EQ(c.offset, 0u);
    EXPECT_EQ(ring.statistics().occupied_bytes, 15u);
    EXPECT_TRUE(ring.release(b.id).is_ok());
    EXPECT_TRUE(ring.release(c.id).is_ok());
    auto all = ring.allocate(maximum, 1).unwrap_unchecked();
    EXPECT_TRUE(ring.allocate(1, 1).is_err());
    EXPECT_TRUE(ring.release(all.id).is_ok());
    alloc::StackRangeAllocator empty_stack(0);
    alloc::RingRangeAllocator  empty_ring(0);
    EXPECT_TRUE(empty_stack.allocate(1, 1).is_err());
    EXPECT_TRUE(empty_ring.allocate(1, 1).is_err());
}

TEST(LinearRange, MetadataFailureAndReleaseWithoutAllocation) {
    LinearFailure state { 0 };
    {
        alloc::StackRangeAllocator stack(4096, LinearMetadata { &state });
        EXPECT_EQ(stack.allocate(1, 1).unwrap_err_unchecked(),
                  alloc::RangeError::MetadataAllocation);
        EXPECT_EQ(stack.statistics().allocation_count, 0u);
        state.remaining = -1;
        auto first      = stack.allocate(1, 1).unwrap_unchecked();
        state.remaining = 0;
        bool failed     = false;
        for (unsigned i = 0; i < 1000; ++i) {
            auto before = stack.statistics();
            auto result = stack.allocate(1, 8);
            if (result.is_err()) {
                EXPECT_EQ(result.unwrap_err_unchecked(), alloc::RangeError::MetadataAllocation);
                EXPECT_EQ(stack.statistics().occupied_bytes, before.occupied_bytes);
                failed = true;
                break;
            }
        }
        EXPECT_TRUE(failed);
        EXPECT_TRUE(stack.get(first.id).is_some());
        const auto calls = state.calls;
        while (auto top = stack.top()) ASSERT_TRUE(stack.pop(top->id).is_ok());
        EXPECT_EQ(state.calls, calls);
    }
    EXPECT_EQ(state.live, 0);
    state.remaining = 0;
    {
        alloc::RingRangeAllocator ring(4096, LinearMetadata { &state });
        EXPECT_EQ(ring.allocate(1, 1).unwrap_err_unchecked(),
                  alloc::RangeError::MetadataAllocation);
        EXPECT_EQ(ring.statistics().allocation_count, 0u);
        state.remaining = -1;
        auto first      = ring.allocate(1, 1).unwrap_unchecked();
        state.remaining = 0;
        bool failed     = false;
        for (unsigned i = 0; i < 1000; ++i) {
            auto before = ring.statistics();
            auto result = ring.allocate(1, 8);
            if (result.is_err()) {
                EXPECT_EQ(result.unwrap_err_unchecked(), alloc::RangeError::MetadataAllocation);
                EXPECT_EQ(ring.statistics().occupied_bytes, before.occupied_bytes);
                failed = true;
                break;
            }
        }
        EXPECT_TRUE(failed);
        EXPECT_TRUE(ring.get(first.id).is_some());
        const auto calls = state.calls;
        while (auto front = ring.front()) ASSERT_TRUE(ring.release(front->id).is_ok());
        EXPECT_EQ(state.calls, calls);
        for (unsigned i = 0; i < 100; ++i) {
            auto result = ring.allocate(1, 1);
            ASSERT_TRUE(result.is_ok());
            ASSERT_TRUE(ring.release(result.unwrap_unchecked().id).is_ok());
        }
        EXPECT_EQ(state.calls, calls);
    }
    EXPECT_EQ(state.live, 0);
}

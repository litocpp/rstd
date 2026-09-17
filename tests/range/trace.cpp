#include <rstd/test/gtest.hpp>
#include <cstdio>
import rstd;
using namespace rstd::prelude;

namespace
{
struct Span {
    alloc::RangeSize offset {}, size {};
};
struct FirstFit {
    Span             free[1025] {};
    unsigned         count { 1 };
    alloc::RangeSize searches {};
    explicit FirstFit(alloc::RangeSize capacity) { free[0] = { 0, capacity }; }
    auto allocate(alloc::RangeSize size, alloc::RangeSize alignment) -> Option<Span> {
        for (unsigned i = 0; i < count; ++i) {
            ++searches;
            const auto old = free[i];
            auto       offset =
                alloc::AlignedRangeOffset(old.offset, size, alignment, old.offset + old.size);
            if (offset.is_none()) continue;
            for (unsigned j = i; j + 1 < count; ++j) free[j] = free[j + 1];
            --count;
            if (*offset > old.offset) deallocate({ old.offset, *offset - old.offset });
            if (*offset + size < old.offset + old.size)
                deallocate({ *offset + size, old.offset + old.size - *offset - size });
            return Some(Span { *offset, size });
        }
        return None();
    }
    void deallocate(Span span) {
        unsigned i = 0;
        while (i < count && free[i].offset < span.offset) ++i;
        for (unsigned j = count; j > i; --j) free[j] = free[j - 1];
        free[i] = span;
        ++count;
        if (i && free[i - 1].offset + free[i - 1].size == free[i].offset) {
            free[i - 1].size += free[i].size;
            for (unsigned j = i; j + 1 < count; ++j) free[j] = free[j + 1];
            --count;
            --i;
        }
        if (i + 1 < count && free[i].offset + free[i].size == free[i + 1].offset) {
            free[i].size += free[i + 1].size;
            for (unsigned j = i + 1; j + 1 < count; ++j) free[j] = free[j + 1];
            --count;
        }
    }
    auto largest() const -> alloc::RangeSize {
        alloc::RangeSize result = 0;
        for (unsigned i = 0; i < count; ++i)
            if (free[i].size > result) result = free[i].size;
        return result;
    }
};
struct Event {
    unsigned slot, size, alignment;
};
struct Metrics {
    unsigned           failures {};
    alloc::RangeSize   searches {}, metadata {}, largest {}, free {};
    unsigned long long nanos {};
};
} // namespace
TEST(Range, DeterministicTraceComparison) {
    Event          events[20000];
    rstd::uint32_t random = 0x7a9135;
    for (auto& e : events) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        e = { unsigned(random % 128),
              unsigned(16 + (random >> 7) % 4081),
              1U << ((random >> 23) % 9) };
    }
    const alloc::RangeSize capacities[] { 65536, 1048576 };
    for (auto capacity : capacities) {
        Metrics segregated, linear;
        {
            alloc::RangeAllocator  ranges(capacity);
            alloc::RangeAllocation live[128] {};
            bool                   occupied[128] {};
            auto                   start = rstd::time::Instant::now();
            for (auto e : events) {
                if (occupied[e.slot]) ASSERT_TRUE(ranges.deallocate(live[e.slot].id).is_ok());
                auto result      = ranges.allocate(e.size, e.alignment);
                occupied[e.slot] = result.is_ok();
                if (occupied[e.slot])
                    live[e.slot] = result.unwrap_unchecked();
                else
                    ++segregated.failures;
            }
            segregated.nanos =
                static_cast<unsigned long long>(start.elapsed().as_nanos().to_primitive());
            auto stats          = ranges.statistics();
            segregated.searches = stats.search_steps;
            segregated.metadata = stats.metadata_bytes;
            segregated.largest  = stats.largest_free_range;
            segregated.free     = stats.free_bytes;
            for (unsigned i = 0; i < 128; ++i)
                if (occupied[i]) ASSERT_TRUE(ranges.deallocate(live[i].id).is_ok());
            EXPECT_EQ(ranges.statistics().largest_free_range, capacity);
        }
        {
            FirstFit ranges(capacity);
            Span     live[128] {};
            bool     occupied[128] {};
            auto     start = rstd::time::Instant::now();
            for (auto e : events) {
                if (occupied[e.slot]) ranges.deallocate(live[e.slot]);
                auto result      = ranges.allocate(e.size, e.alignment);
                occupied[e.slot] = result.is_some();
                if (occupied[e.slot])
                    live[e.slot] = *result;
                else
                    ++linear.failures;
            }
            linear.nanos =
                static_cast<unsigned long long>(start.elapsed().as_nanos().to_primitive());
            linear.searches = ranges.searches;
            linear.metadata = sizeof(ranges);
            linear.largest  = ranges.largest();
            for (unsigned i = 0; i < ranges.count; ++i) linear.free += ranges.free[i].size;
            for (unsigned i = 0; i < 128; ++i)
                if (occupied[i]) ranges.deallocate(live[i]);
            EXPECT_EQ(ranges.largest(), capacity);
            EXPECT_EQ(ranges.count, 1u);
        }
        std::printf("trace capacity=%llu events=20000 slots=128 seed=0x7a9135\n",
                    static_cast<unsigned long long>(capacity));
        auto print = [](const char* name, const Metrics& m) {
            std::printf("%s: failures=%u search_steps=%llu metadata_reserved=%llu final_free=%llu "
                        "largest_gap=%llu elapsed_ns=%llu\n",
                        name,
                        m.failures,
                        static_cast<unsigned long long>(m.searches),
                        static_cast<unsigned long long>(m.metadata),
                        static_cast<unsigned long long>(m.free),
                        static_cast<unsigned long long>(m.largest),
                        m.nanos);
        };
        print("segregated", segregated);
        print("first_fit", linear);
        if (capacity == 1048576) {
            EXPECT_EQ(segregated.failures, 0u);
            EXPECT_EQ(linear.failures, 0u);
        }
    }
}

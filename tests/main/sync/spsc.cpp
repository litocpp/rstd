#include <rstd/test/gtest.hpp>
#include <atomic>
#include <thread>
#include <type_traits>
import rstd;

using namespace rstd;
using namespace rstd::sync::spsc;

struct AllocationStats {
    std::atomic<int> allocations {}, deallocations {};
    bool             fail {};
};
struct TestAllocator {
    AllocationStats* stats;
};
namespace rstd
{
template<>
struct Impl<alloc::Allocator, TestAllocator> : DefaultInImpl<alloc::Allocator, TestAllocator> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        auto* stats = this->self().stats;
        ++stats->allocations;
        if (stats->fail) return Err(alloc::AllocError {});
        return as<alloc::Allocator>(::alloc::GLOBAL).allocate(layout);
    }
    void deallocate(void* ptr, alloc::Layout layout) const noexcept {
        ++this->self().stats->deallocations;
        as<alloc::Allocator>(::alloc::GLOBAL).deallocate(ptr, layout);
    }
};
} // namespace rstd
namespace
{
struct Value {
    std::atomic<int>* live;
    int               number;
    Value(std::atomic<int>& count, int n) noexcept: live(&count), number(n) { ++*live; }
    Value(const Value&) = delete;
    Value(Value&& other) noexcept: live(other.live), number(other.number) { ++*live; }
    ~Value() { --*live; }
};
static_assert(! std::is_copy_constructible_v<Producer<int>>);
static_assert(! std::is_copy_constructible_v<Consumer<int>>);
static_assert(! std::is_copy_constructible_v<ReadGuard<int>>);
static_assert(std::is_nothrow_move_constructible_v<Producer<int>>);
static_assert(std::is_nothrow_move_constructible_v<Consumer<int>>);

TEST(Spsc, CapacityAndWrap) {
    EXPECT_TRUE(RingBuffer<int>::make(usize()).unwrap_err().is_InvalidCapacity());
    EXPECT_TRUE(RingBuffer<int>::make(usize::MAX).unwrap_err().is_InvalidCapacity());
    for (unsigned capacity : { 1u, 2u, 3u, 7u, 32u }) {
        auto [producer, consumer] = RingBuffer<int>::make(usize(capacity)).unwrap();
        EXPECT_EQ(producer.capacity(), usize(capacity));
        EXPECT_EQ(consumer.capacity(), usize(capacity));
        for (int round = 0; round < 300; ++round) {
            EXPECT_TRUE(consumer.try_pop().unwrap_err().is_Empty());
            for (unsigned i = 0; i < capacity; ++i)
                ASSERT_TRUE(producer.try_push(round * 100 + int(i)).is_ok());
            EXPECT_TRUE(producer.is_full());
            auto full = producer.try_push(9999);
            ASSERT_TRUE(full.is_err());
            EXPECT_EQ(full.unwrap_err().as_Full().value, 9999);
            for (unsigned i = 0; i < capacity; ++i)
                EXPECT_EQ(consumer.try_pop().unwrap(), round * 100 + int(i));
            EXPECT_TRUE(producer.is_empty());
        }
    }
}
TEST(Spsc, BorrowWithoutConsumingAndMoveGuard) {
    auto [producer, consumer] = RingBuffer<int>::make(usize(1)).unwrap();
    ASSERT_TRUE(producer.try_push(42).is_ok());
    {
        auto guard = consumer.try_front().unwrap();
        auto moved = rstd::move(guard);
        EXPECT_EQ(*moved, 42);
        EXPECT_TRUE(producer.try_push(99).is_err());
        producer.close();
        EXPECT_EQ(*moved, 42);
    }
    auto guard = consumer.try_front().unwrap();
    EXPECT_EQ(*guard, 42);
    guard.consume();
    EXPECT_TRUE(consumer.try_front().unwrap_err().is_Disconnected());
}
TEST(Spsc, ActiveBorrowRejectsConflictingOperations) {
    auto [producer, consumer] = RingBuffer<int>::make(usize(1)).unwrap();
    ASSERT_TRUE(producer.try_push(5).is_ok());
    auto guard = consumer.try_front().unwrap();
    EXPECT_DEATH((void)consumer.try_pop(), "active read guard");
    EXPECT_DEATH((void)consumer.try_front(), "active read guard");
    EXPECT_DEATH(consumer.close(), "active read guard");
    EXPECT_DEATH({ auto moved = rstd::move(consumer); }, "active read guard");
    guard.consume();
    EXPECT_DEATH(guard.consume(), "already consumed");
}
TEST(Spsc, ProducerCloseDrainsAllRemainingElements) {
    auto [producer, consumer] = RingBuffer<int>::make(usize(3)).unwrap();
    for (int i = 0; i < 3; ++i) ASSERT_TRUE(producer.try_push(i).is_ok());
    producer.close();
    for (int i = 0; i < 3; ++i) EXPECT_EQ(consumer.try_pop().unwrap(), i);
    EXPECT_TRUE(consumer.try_pop().unwrap_err().is_Disconnected());
}
TEST(Spsc, BorrowMoveAssignmentEndsOnlyTheOldBorrow) {
    auto [p, c]   = RingBuffer<int>::make(usize(1)).unwrap();
    auto [p2, c2] = RingBuffer<int>::make(usize(1)).unwrap();
    ASSERT_TRUE(p.try_push(1).is_ok());
    ASSERT_TRUE(p2.try_push(2).is_ok());
    auto first  = c.try_front().unwrap();
    auto second = c2.try_front().unwrap();
    first       = rstd::move(second);
    EXPECT_EQ(c.try_pop().unwrap(), 1);
    EXPECT_EQ(*first, 2);
    first.consume();
    EXPECT_TRUE(c2.is_empty());
}
TEST(Spsc, MoveOnlyLifetimeAndNoHotPathAllocation) {
    AllocationStats  stats;
    std::atomic<int> live {};
    {
        auto [producer, consumer] =
            RingBuffer<Value, TestAllocator>::make(usize(3), { &stats }).unwrap();
        EXPECT_EQ(live.load(), 0);
        EXPECT_EQ(stats.allocations.load(), 1);
        for (int i = 0; i < 500; ++i) {
            ASSERT_TRUE(producer.try_push(Value(live, i)).is_ok());
            auto value = consumer.try_pop().unwrap();
            EXPECT_EQ(value.number, i);
        }
        EXPECT_EQ(stats.allocations.load(), 1);
        EXPECT_EQ(stats.deallocations.load(), 0);
        ASSERT_TRUE(producer.try_push(Value(live, 9)).is_ok());
        producer.close();
        EXPECT_EQ(live.load(), 1);
        EXPECT_EQ(stats.deallocations.load(), 0);
    }
    EXPECT_EQ(live.load(), 0);
    EXPECT_EQ(stats.deallocations.load(), 1);
}
TEST(Spsc, FullAndDisconnectedReturnOriginalMoveOnlyValue) {
    std::atomic<int> live {};
    {
        auto [producer, consumer] = RingBuffer<Value>::make(usize(1)).unwrap();
        ASSERT_TRUE(producer.try_push(Value(live, 1)).is_ok());
        {
            auto result = producer.try_push(Value(live, 2));
            EXPECT_EQ(result.unwrap_err().as_Full().value.number, 2);
        }
        consumer.close();
        EXPECT_TRUE(producer.is_disconnected());
        auto result = producer.try_push(Value(live, 3));
        EXPECT_EQ(result.unwrap_err().as_Disconnected().value.number, 3);
    }
    EXPECT_EQ(live.load(), 0);
}
TEST(Spsc, AllocationFailureAndEndpointMoveAssignment) {
    AllocationStats stats;
    stats.fail = true;
    EXPECT_TRUE((RingBuffer<int, TestAllocator>::make(usize(1), { &stats })
                     .unwrap_err()
                     .is_AllocationFailed()));
    EXPECT_EQ(stats.deallocations.load(), 0);
    auto [p, c]   = RingBuffer<int>::make(usize(1)).unwrap();
    auto [p2, c2] = RingBuffer<int>::make(usize(2)).unwrap();
    p             = rstd::move(p2);
    EXPECT_TRUE(c.try_pop().unwrap_err().is_Disconnected());
    c = rstd::move(c2);
    ASSERT_TRUE(p.try_push(7).is_ok());
    EXPECT_EQ(c.try_pop().unwrap(), 7);
    EXPECT_TRUE(p2.try_push(1).unwrap_err().is_Disconnected());
    EXPECT_TRUE(c2.try_pop().unwrap_err().is_Disconnected());
}
struct Packet {
    std::uint64_t sequence, checksum;
};
TEST(Spsc, ConcurrentOrderedPayloadAndProducerClose) {
    auto [producer, consumer]     = RingBuffer<Packet>::make(usize(7)).unwrap();
    constexpr std::uint64_t count = 200000;
    std::thread             writer([p = rstd::move(producer)]() mutable {
        for (std::uint64_t i = 0; i < count; ++i)
            while (p.try_push(Packet { i, ~i }).is_err()) std::this_thread::yield();
    });
    std::uint64_t           next    = 0;
    bool                    correct = true;
    for (;;) {
        auto result = consumer.try_front();
        if (result.is_err()) {
            if (result.unwrap_err().is_Disconnected()) break;
            std::this_thread::yield();
            continue;
        }
        auto guard = rstd::move(result).unwrap();
        correct    = correct && guard->sequence == next && guard->checksum == ~next;
        ++next;
        guard.consume();
    }
    writer.join();
    EXPECT_TRUE(correct);
    EXPECT_EQ(next, count);
}
TEST(Spsc, ConsumerCloseRacesPushWithoutLeaking) {
    for (int round = 0; round < 100; ++round) {
        AllocationStats  stats;
        std::atomic<int> live {};
        auto [producer, consumer] =
            RingBuffer<Value, TestAllocator>::make(usize(3), { &stats }).unwrap();
        std::atomic<bool> started {};
        std::thread       writer([p = rstd::move(producer), &live, &started]() mutable {
            started = true;
            for (int i = 0;; ++i) {
                auto result = p.try_push(Value(live, i));
                if (result.is_err() && result.unwrap_err().is_Disconnected()) break;
            }
        });
        while (! started) std::this_thread::yield();
        consumer.close();
        writer.join();
        EXPECT_EQ(live.load(), 0);
        EXPECT_EQ(stats.allocations.load(), stats.deallocations.load());
    }
}
struct alignas(256) Aligned {
    int value;
};
TEST(Spsc, AllocatorReferencePreservesResourceAndAlignment) {
    AllocationStats stats;
    TestAllocator   resource { &stats };
    {
        auto [producer, consumer] = RingBuffer<Aligned, ref<dyn<rstd::alloc::Allocator>>>::make(
                                        usize(3), ::alloc::allocator_ref(resource))
                                        .unwrap();
        ASSERT_TRUE(producer.try_push(Aligned { 12 }).is_ok());
        auto guard = consumer.try_front().unwrap();
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(guard.operator->()) % alignof(Aligned), 0u);
        EXPECT_EQ(guard->value, 12);
        guard.consume();
        EXPECT_EQ(stats.allocations.load(), 1);
        EXPECT_EQ(stats.deallocations.load(), 0);
    }
    EXPECT_EQ(stats.deallocations.load(), 1);
}
} // namespace

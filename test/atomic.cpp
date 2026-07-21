#include <gtest/gtest.h>
#include <type_traits>
import rstd;

using namespace rstd;
using sync::atomic::Atomic;
using sync::atomic::Ordering;

static_assert(std::is_same_v<Atomic<u32>::Native, rstd::uint32_t>);
static_assert(std::is_same_v<Atomic<u64>::Native, rstd::uint64_t>);
static_assert(std::is_same_v<Atomic<usize>::Native, rstd::size_t>);

namespace
{

struct alignas(4) Pair {
    float x;
    float y;

    friend constexpr bool operator==(Pair, Pair) = default;
};

static_assert(sizeof(Pair) == 8);
static_assert(alignof(Pair) == 4);
static_assert(alignof(Atomic<Pair>) == 8);

} // namespace

TEST(AtomicNumeric, LoadStoreExchangeAndCompareExchange) {
    auto value = Atomic<u32> { u32(1) };
    EXPECT_EQ(value.load(Ordering::Relaxed), u32(1));

    value.store(u32(2), Ordering::Release);
    EXPECT_EQ(value.exchange(u32(3), Ordering::AcqRel), u32(2));

    auto expected = u32(3);
    EXPECT_TRUE(
        value.compare_exchange_strong(expected, u32(4), Ordering::AcqRel, Ordering::Acquire));
    EXPECT_EQ(value.load(Ordering::Acquire), u32(4));

    expected = u32(9);
    EXPECT_FALSE(
        value.compare_exchange_strong(expected, u32(5), Ordering::AcqRel, Ordering::Acquire));
    EXPECT_EQ(expected, u32(4));
}

TEST(AtomicNumeric, FetchOperationsUseNativeStorage) {
    auto count = Atomic<usize> { usize(5) };
    EXPECT_EQ(count.fetch_add(usize(2)), usize(5));
    EXPECT_EQ(count.fetch_sub(usize(1)), usize(7));
    EXPECT_EQ(count.load(), usize(6));

    auto bits = Atomic<u64> { u64(0b1010) };
    EXPECT_EQ(bits.fetch_or(u64(0b0101)), u64(0b1010));
    EXPECT_EQ(bits.fetch_and(u64(0b1100)), u64(0b1111));
    EXPECT_EQ(bits.fetch_xor(u64(0b1000)), u64(0b1100));
    EXPECT_EQ(bits.load(), u64(0b0100));
}

TEST(AtomicNumeric, ExposesPrimitiveAddressForPalOwners) {
    auto value = Atomic<u32> { u32(7) };
    static_assert(std::is_same_v<decltype(value.as_native_ptr()), rstd::uint32_t*>);
    EXPECT_NE(value.as_native_ptr(), nullptr);
    EXPECT_EQ(*value.as_native_ptr(), rstd::uint32_t(7));
}

TEST(AtomicNumeric, AlignsAggregateStorageForAtomicOperations) {
    auto value = Atomic<Pair> { Pair { 1.0f, 2.0f } };
    EXPECT_EQ(value.load(), (Pair { 1.0f, 2.0f }));
    value.store(Pair { 3.0f, 4.0f });
    EXPECT_EQ(value.load(), (Pair { 3.0f, 4.0f }));
}

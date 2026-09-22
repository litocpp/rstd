#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

constexpr bool swap_proxy_handles() {
    rstd::byte data[] = { rstd::byte { 3 }, rstd::byte { 1 } };
    auto       a      = mut_ref<u8>::from_raw_parts(data);
    auto       b      = mut_ref<u8>::from_raw_parts(data + 1);
    rstd::mem::swap(a, b);
    rstd::mem::swap(a, a);
    if (a.as_raw_ptr() != data + 1 || b.as_raw_ptr() != data) return false;
    if (data[0] != rstd::byte { 3 } || data[1] != rstd::byte { 1 }) return false;
    a = b;
    return a.as_raw_ptr() == data + 1 && data[1] == rstd::byte { 3 };
}
static_assert(swap_proxy_handles());

TEST(Mem, SwapProxyHandlesPreservesWriteThroughAssignment) {
    EXPECT_TRUE(swap_proxy_handles());
}

struct SwapLifetime {
    int constructed = 0;
    int destroyed   = 0;
};

struct SwapValue {
    int           value;
    SwapLifetime* counts;
    SwapValue(int value, SwapLifetime& counts): value(value), counts(&counts) {
        ++counts.constructed;
    }
    SwapValue(SwapValue&& other) noexcept: value(other.value), counts(other.counts) {
        ++counts->constructed;
    }
    SwapValue(const SwapValue&)               = delete;
    auto operator=(SwapValue&&) -> SwapValue& = delete;
    ~SwapValue() { ++counts->destroyed; }
};

TEST(Mem, SwapNonAssignableObjectsAndSelf) {
    SwapLifetime counts;
    {
        SwapValue a(1, counts), b(2, counts);
        rstd::mem::swap(a, b);
        EXPECT_EQ(a.value, 2);
        EXPECT_EQ(b.value, 1);
        EXPECT_EQ(counts.constructed - counts.destroyed, 2);
        const auto constructed = counts.constructed;
        const auto destroyed   = counts.destroyed;
        rstd::mem::swap(a, a);
        EXPECT_EQ(counts.constructed, constructed);
        EXPECT_EQ(counts.destroyed, destroyed);
    }
    EXPECT_EQ(counts.constructed, counts.destroyed);
}

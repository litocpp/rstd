#include <rstd/test/gtest.hpp>
#include <type_traits>

import rstd;

using namespace rstd::prelude;
using rstd::alloc::Layout;
using alloc::vec::Vec;

namespace
{
struct alignas(64) LayoutAligned {
    unsigned char value;
};
struct LayoutThreeBytes {
    unsigned char values[3];
};
constexpr auto layout_limit = usize(static_cast<rstd::size_t>(isize::MAX.to_primitive()));
constexpr auto empty_layout = Layout();
static_assert(! std::is_aggregate_v<Layout>);
static_assert(empty_layout.size() == usize() && empty_layout.align() == usize(1));
static_assert(Layout::make<LayoutAligned>().align() == usize(64));
static_assert(Layout::array<u8>(usize(7))->size() == usize(7));
static_assert(Layout::array<LayoutAligned>(usize(2))->size() == usize(128));
static_assert(Layout::from_size_align(usize(1), usize(3)).is_none());
static_assert(Layout::array<u8>(layout_limit).is_some());
static_assert(Layout::array<u8>(layout_limit + usize(1)).is_none());
static_assert(Layout::array<u8>(usize(3), usize(64))->size() == usize(3));
static_assert(Layout::array<LayoutAligned>(usize(1), usize(32)).is_none());

struct LayoutFailingAllocator {
    int* calls;
};
} // namespace

template<>
struct rstd::Impl<rstd::alloc::Allocator, LayoutFailingAllocator>
    : rstd::DefaultInImpl<rstd::alloc::Allocator, LayoutFailingAllocator> {
    auto allocate(Layout) const -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        ++*this->self().calls;
        return Err(rstd::alloc::AllocError {});
    }
    void deallocate(void*, Layout) const noexcept {}
};

TEST(Layout, CheckedConstructionRejectsInvalidAlignmentWithoutPanicking) {
    for (auto alignment : { usize(), usize(3), usize(6), usize::MAX })
        EXPECT_TRUE(Layout::from_size_align(usize(1), alignment).is_none());
    for (auto alignment : { usize(1), usize(8), usize(64), layout_limit + usize(1) }) {
        auto zero = Layout::from_size_align(usize(), alignment).unwrap();
        EXPECT_EQ(zero.size(), usize());
        EXPECT_EQ(zero.align(), alignment);
    }
}

TEST(Layout, SizeLimitIncludesTrailingAlignmentPadding) {
    EXPECT_TRUE(Layout::from_size_align(layout_limit, usize(1)).is_some());
    EXPECT_TRUE(Layout::from_size_align(layout_limit + usize(1), usize(1)).is_none());
    EXPECT_TRUE(Layout::from_size_align(usize::MAX, usize(1)).is_none());
    for (auto alignment : { usize(2), usize(8), usize(256) }) {
        auto limit = layout_limit - (alignment - usize(1));
        auto valid = Layout::from_size_align(limit, alignment).unwrap();
        EXPECT_EQ(valid.pad_to_align().size(), limit);
        EXPECT_TRUE(Layout::from_size_align(limit + usize(1), alignment).is_none());
    }
    EXPECT_TRUE(Layout::from_size_align(usize(1), layout_limit + usize(1)).is_none());
}

TEST(Layout, ArrayThresholdHandlesByteOddSizedAndOveralignedElements) {
    EXPECT_EQ(Layout::array<u8>(layout_limit).unwrap().size(), layout_limit);
    auto odd_limit = layout_limit / usize(sizeof(LayoutThreeBytes));
    EXPECT_TRUE(Layout::array<LayoutThreeBytes>(odd_limit).is_some());
    EXPECT_TRUE(Layout::array<LayoutThreeBytes>(odd_limit + usize(1)).is_none());
    auto aligned_limit = layout_limit / usize(sizeof(LayoutAligned));
    EXPECT_TRUE(Layout::array<LayoutAligned>(aligned_limit).is_some());
    EXPECT_TRUE(Layout::array<LayoutAligned>(aligned_limit + usize(1)).is_none());
    auto zero = Layout::array<LayoutAligned>(usize()).unwrap();
    EXPECT_EQ(zero.size(), usize());
    EXPECT_EQ(zero.align(), usize(64));
}

TEST(Layout, ExplicitArrayAlignmentDoesNotAddElementStridePadding) {
    auto bytes = Layout::array<u8>(usize(3), usize(256)).unwrap();
    EXPECT_EQ(bytes.size(), usize(3));
    EXPECT_EQ(bytes.align(), usize(256));
    auto limit = layout_limit - usize(255);
    EXPECT_TRUE(Layout::array<u8>(limit, usize(256)).is_some());
    EXPECT_TRUE(Layout::array<u8>(limit + usize(1), usize(256)).is_none());
    EXPECT_TRUE(Layout::array<LayoutAligned>(usize(2), usize(32)).is_none());
    EXPECT_TRUE(Layout::array<u8>(usize(), usize()).is_none());
    EXPECT_TRUE(Layout::array<u8>(usize(1), usize(3)).is_none());
}

TEST(Layout, ExtendAndAlignmentAdjustmentPreserveInvariantsAndFailureOffset) {
    auto head     = Layout::from_size_align(usize(3), usize(1)).unwrap();
    auto next     = Layout::from_size_align(usize(5), usize(8)).unwrap();
    auto offset   = usize(99);
    auto extended = head.extend(next, offset).unwrap();
    EXPECT_EQ(offset, usize(8));
    EXPECT_EQ(extended.size(), usize(13));
    EXPECT_EQ(extended.align(), usize(8));
    EXPECT_EQ(extended.pad_to_align().size(), usize(16));
    EXPECT_EQ(extended.align_to(usize(2))->align(), usize(8));
    EXPECT_TRUE(extended.align_to(usize(3)).is_none());
    auto large = Layout::from_size_align(layout_limit, usize(1)).unwrap();
    offset     = usize(99);
    EXPECT_TRUE(large.extend(Layout::make<u8>(), offset).is_none());
    EXPECT_EQ(offset, usize(99));
    EXPECT_TRUE(large.align_to(usize(8)).is_none());
    EXPECT_TRUE(large.cpp_layout().is_none());
    auto cpp = head.cpp_layout().unwrap();
    EXPECT_EQ(cpp.size(), head.size());
    EXPECT_GE(cpp.align(), usize(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
}

TEST(Layout, VecRejectsInvalidOrOversizedAlignedRequestsBeforeAllocation) {
    int calls    = 0;
    using Values = Vec<u8, LayoutFailingAllocator>;
    using Error  = alloc::collections::TryReserveError;
    for (auto alignment : { usize(), usize(3) }) {
        auto value = Values::try_with_capacity_aligned_in(
            usize(1), alignment, LayoutFailingAllocator { &calls });
        EXPECT_EQ(value.unwrap_err(), Error::CapacityOverflow);
    }
    auto oversized = Values::try_with_capacity_aligned_in(
        layout_limit, usize(64), LayoutFailingAllocator { &calls });
    EXPECT_EQ(oversized.unwrap_err(), Error::CapacityOverflow);
    EXPECT_EQ(calls, 0);
    auto valid = Values::try_with_capacity_aligned_in(
        usize(3), usize(64), LayoutFailingAllocator { &calls });
    EXPECT_EQ(valid.unwrap_err(), Error::AllocError);
    EXPECT_EQ(calls, 1);
}

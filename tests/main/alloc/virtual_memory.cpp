#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;
using rstd::alloc::Allocator;
using rstd::alloc::Layout;
using rstd::alloc::VirtualMemoryAllocator;

TEST(VirtualMemoryAllocator, MapsAlignedZeroedPagesAndReleasesThem) {
    VirtualMemoryAllocator allocator;
    auto const             layout =
        Layout::from_size_align(allocator.page_size() + usize(1), usize(16)).unwrap();
    auto allocation = rstd::as<Allocator>(allocator).allocate_zeroed(layout).unwrap();

    EXPECT_EQ(reinterpret_cast<uintptr_t>(allocation.pointer) %
                  allocator.mapping_alignment().to_primitive(),
              0u);
    auto* bytes = static_cast<unsigned char*>(allocation.pointer);
    EXPECT_EQ(bytes[0], 0u);
    EXPECT_EQ(bytes[layout.size.to_primitive() - 1], 0u);

    auto statistics = allocator.statistics();
    EXPECT_EQ(statistics.mapped_bytes, allocator.page_size() * usize(2));
    EXPECT_EQ(statistics.mappings, usize(1));
    EXPECT_EQ(statistics.allocation_calls, usize(1));

    rstd::as<Allocator>(allocator).deallocate(allocation.pointer, layout);
    statistics = allocator.statistics();
    EXPECT_EQ(statistics.mapped_bytes, usize());
    EXPECT_EQ(statistics.mappings, usize());
    EXPECT_EQ(statistics.deallocation_calls, usize(1));
}

TEST(VirtualMemoryAllocator, ZeroSizedAllocationDoesNotCreateMapping) {
    VirtualMemoryAllocator allocator;
    auto const             layout     = Layout::from_size_align(usize(), usize(16)).unwrap();
    auto                   allocation = rstd::as<Allocator>(allocator).allocate(layout).unwrap();

    EXPECT_EQ(allocation.pointer, layout.dangling());
    EXPECT_EQ(allocator.statistics().mappings, usize());
    rstd::as<Allocator>(allocator).deallocate(allocation.pointer, layout);
    EXPECT_EQ(allocator.statistics().deallocation_calls, usize());
}

TEST(VirtualMemoryAllocator, RejectsUnsupportedAlignmentAndOverflow) {
    VirtualMemoryAllocator allocator;
    auto const             alignment    = allocator.mapping_alignment() * usize(2);
    auto const             over_aligned = Layout::from_size_align(usize(64), alignment).unwrap();
    EXPECT_TRUE(rstd::as<Allocator>(allocator).allocate(over_aligned).is_err());

    auto const overflowing = Layout::from_size_align_unchecked(usize::MAX, usize(1));
    EXPECT_TRUE(rstd::as<Allocator>(allocator).allocate(overflowing).is_err());
    EXPECT_EQ(allocator.statistics().mappings, usize());
}

TEST(VirtualMemoryAllocator, ReportsOperatingSystemAllocationFailure) {
    VirtualMemoryAllocator allocator;
    auto const             size   = usize::MAX - (usize::MAX % allocator.page_size());
    auto const             layout = Layout::from_size_align(size, usize(1)).unwrap();

    EXPECT_TRUE(rstd::as<Allocator>(allocator).allocate(layout).is_err());
    EXPECT_EQ(allocator.statistics().mappings, usize());
}

TEST(VirtualMemoryAllocator, MoveTransfersLiveMappingStatistics) {
    VirtualMemoryAllocator source;
    auto const             layout = Layout::from_size_align(source.page_size(), usize(16)).unwrap();
    auto                   allocation = rstd::as<Allocator>(source).allocate(layout).unwrap();

    VirtualMemoryAllocator destination(rstd::move(source));
    EXPECT_EQ(source.statistics().mappings, usize());
    EXPECT_EQ(destination.statistics().mappings, usize(1));

    rstd::as<Allocator>(destination).deallocate(allocation.pointer, layout);
    EXPECT_EQ(destination.statistics().mappings, usize());
}

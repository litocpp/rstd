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
    EXPECT_EQ(bytes[layout.size().to_primitive() - 1], 0u);

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

    auto const overflowing =
        Layout::from_size_align(usize(isize::MAX.to_primitive()), usize(1)).unwrap();
    EXPECT_TRUE(rstd::as<Allocator>(allocator).allocate(overflowing).is_err());
    EXPECT_EQ(allocator.statistics().mappings, usize());
}

TEST(VirtualMemoryAllocator, ReportsOperatingSystemAllocationFailure) {
    if constexpr (sizeof(rstd::size_t) < 8) GTEST_SKIP() << "requires a 64-bit address space";
    VirtualMemoryAllocator allocator;
    auto const             maximum = usize(isize::MAX.to_primitive());
    auto const             size    = maximum - (maximum % allocator.page_size());
    auto const             layout  = Layout::from_size_align(size, usize(1)).unwrap();

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

TEST(VirtualMemoryAllocator, DynResizePreservesDataAndReleasesMappings) {
    VirtualMemoryAllocator concrete;
    auto                   allocator = rstd::alloc::allocator_ref(concrete);
    {
        auto layout    = Layout::from_size_align(usize(64), usize(16)).unwrap();
        auto allocated = allocator->allocate(layout);
        ASSERT_TRUE(allocated.is_ok());
        struct LiveAllocation {
            ref<dyn<Allocator>>     allocator;
            rstd::alloc::Allocation allocation;
            Layout                  layout;

            ~LiveAllocation() { allocator->deallocate(allocation.pointer, layout); }
        } live { allocator, allocated.unwrap(), layout };
        auto* bytes = static_cast<unsigned char*>(live.allocation.pointer);
        for (rstd::size_t index = 0; index < 64; ++index) bytes[index] = 0x5a;

        auto grown_layout = Layout::from_size_align(usize(256), usize(32)).unwrap();
        auto grown        = allocator->grow(live.allocation.pointer, live.layout, grown_layout);
        ASSERT_TRUE(grown.is_ok());
        live.allocation = grown.unwrap();
        live.layout     = grown_layout;
        bytes           = static_cast<unsigned char*>(live.allocation.pointer);
        for (rstd::size_t index = 0; index < 64; ++index) EXPECT_EQ(bytes[index], 0x5a);
        for (rstd::size_t index = 64; index < 256; ++index) bytes[index] = 0x5a;

        auto zeroed_layout = Layout::from_size_align(usize(512), usize(64)).unwrap();
        auto zeroed = allocator->grow_zeroed(live.allocation.pointer, live.layout, zeroed_layout);
        ASSERT_TRUE(zeroed.is_ok());
        live.allocation = zeroed.unwrap();
        live.layout     = zeroed_layout;
        bytes           = static_cast<unsigned char*>(live.allocation.pointer);
        for (rstd::size_t index = 0; index < 256; ++index) EXPECT_EQ(bytes[index], 0x5a);
        for (rstd::size_t index = 256; index < 512; ++index) EXPECT_EQ(bytes[index], 0);

        auto shrunk_layout = Layout::from_size_align(usize(48), usize(16)).unwrap();
        auto shrunk        = allocator->shrink(live.allocation.pointer, live.layout, shrunk_layout);
        ASSERT_TRUE(shrunk.is_ok());
        live.allocation = shrunk.unwrap();
        live.layout     = shrunk_layout;
        bytes           = static_cast<unsigned char*>(live.allocation.pointer);
        for (rstd::size_t index = 0; index < 48; ++index) EXPECT_EQ(bytes[index], 0x5a);

        auto unsupported =
            Layout::from_size_align(usize(128), concrete.mapping_alignment() * usize(2)).unwrap();
        EXPECT_TRUE(allocator->grow(live.allocation.pointer, live.layout, unsupported).is_err());
        for (rstd::size_t index = 0; index < 48; ++index) EXPECT_EQ(bytes[index], 0x5a);
        EXPECT_EQ(concrete.statistics().mappings, usize(1));
    }
    EXPECT_EQ(concrete.statistics().mappings, usize());
    EXPECT_EQ(concrete.statistics().mapped_bytes, usize());

    auto empty_layout = Layout::from_size_align(usize(), usize(16)).unwrap();
    auto empty        = allocator->allocate_zeroed(empty_layout);
    ASSERT_TRUE(empty.is_ok());
    EXPECT_EQ(empty->pointer, empty_layout.dangling());
    allocator->deallocate(empty->pointer, empty_layout);
    EXPECT_EQ(concrete.statistics().mappings, usize());
}

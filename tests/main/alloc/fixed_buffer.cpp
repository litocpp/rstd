#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;

TEST(FixedBufferAllocator, AlignsAgainstActualBufferAddress) {
    alignas(64) rstd::byte            storage[257] {};
    rstd::alloc::FixedBufferAllocator allocator(
        mut_ptr<u8[]>::from_raw_parts(storage + 1, usize(256)));
    auto layout     = rstd::alloc::Layout::from_size_align(usize(8), usize(64)).unwrap();
    auto allocation = allocator.allocate(layout).unwrap();
    EXPECT_EQ(allocation.pointer, storage + 64);
    auto stats = allocator.statistics();
    EXPECT_EQ(stats.capacity_bytes, usize(256));
    EXPECT_EQ(stats.used_bytes, usize(71));
    EXPECT_EQ(stats.padding_bytes, usize(63));
    auto zero  = rstd::alloc::Layout::from_size_align(usize(), usize(4096)).unwrap();
    auto empty = allocator.allocate(zero).unwrap();
    EXPECT_EQ(reinterpret_cast<uintptr_t>(empty.pointer) % 4096, 0u);
    EXPECT_EQ(allocator.statistics().used_bytes, stats.used_bytes);
    EXPECT_EQ(allocator.statistics().padding_bytes, stats.padding_bytes);
}

TEST(FixedBufferAllocator, ExhaustionAndOverflowPreserveState) {
    alignas(16) rstd::byte            storage[32] {};
    rstd::alloc::FixedBufferAllocator allocator(
        mut_ptr<u8[]>::from_raw_parts(storage, usize(sizeof(storage))));
    auto layout = rstd::alloc::Layout::from_size_align(usize(16), usize(16)).unwrap();
    auto first  = allocator.allocate(layout).unwrap();
    rstd::mem::memset(first.pointer, u8(0x5a), first.size);
    auto before    = allocator.statistics();
    auto oversized = rstd::alloc::Layout::from_size_align(usize(33), usize(16)).unwrap();
    EXPECT_TRUE(allocator.allocate(oversized).is_err());
    EXPECT_TRUE(allocator.grow(first.pointer, layout, oversized).is_err());
    auto overflow = rstd::alloc::Layout::from_size_align(usize::MAX, usize(1)).unwrap();
    EXPECT_TRUE(allocator.allocate(overflow).is_err());
    EXPECT_EQ(allocator.statistics().used_bytes, before.used_bytes);
    EXPECT_EQ(allocator.statistics().padding_bytes, before.padding_bytes);
    for (auto index = 0; index < 16; ++index) EXPECT_EQ(storage[index], rstd::byte(0x5a));
}

TEST(FixedBufferAllocator, LastBlockGrowsZeroedAndShrinksInPlace) {
    alignas(16) rstd::byte storage[64];
    rstd::mem::memset(storage, u8(0xa5), usize(sizeof(storage)));
    rstd::alloc::FixedBufferAllocator allocator(
        mut_ptr<u8[]>::from_raw_parts(storage, usize(sizeof(storage))));
    auto borrowed = rstd::alloc::allocator_ref(allocator);
    auto layout   = rstd::alloc::Layout::from_size_align(usize(8), usize(8)).unwrap();
    auto larger   = rstd::alloc::Layout::from_size_align(usize(32), usize(8)).unwrap();
    auto first    = as<rstd::alloc::Allocator>(borrowed).allocate(layout).unwrap();
    auto grown =
        as<rstd::alloc::Allocator>(borrowed).grow_zeroed(first.pointer, layout, larger).unwrap();
    EXPECT_EQ(grown.pointer, first.pointer);
    EXPECT_EQ(allocator.statistics().used_bytes, usize(32));
    for (auto index = 0; index < 8; ++index) EXPECT_EQ(storage[index], rstd::byte(0xa5));
    for (auto index = 8; index < 32; ++index) EXPECT_EQ(storage[index], rstd::byte());
    auto shrunk =
        as<rstd::alloc::Allocator>(borrowed).shrink(grown.pointer, larger, layout).unwrap();
    EXPECT_EQ(shrunk.pointer, first.pointer);
    EXPECT_EQ(allocator.statistics().used_bytes, usize(8));
    auto zero  = rstd::alloc::Layout::from_size_align(usize(), usize(4096)).unwrap();
    auto empty = as<rstd::alloc::Allocator>(borrowed).shrink(shrunk.pointer, layout, zero).unwrap();
    EXPECT_EQ(reinterpret_cast<uintptr_t>(empty.pointer) % 4096, 0u);
    EXPECT_EQ(allocator.statistics().used_bytes, usize());
}

TEST(FixedBufferAllocator, EarlierBlockGrowthUsesRemainingBufferAndCopiesData) {
    alignas(16) rstd::byte            storage[80] {};
    rstd::alloc::FixedBufferAllocator allocator(
        mut_ptr<u8[]>::from_raw_parts(storage, usize(sizeof(storage))));
    auto layout = rstd::alloc::Layout::from_size_align(usize(8), usize(8)).unwrap();
    auto larger = rstd::alloc::Layout::from_size_align(usize(16), usize(8)).unwrap();
    auto first  = allocator.allocate(layout).unwrap();
    auto second = allocator.allocate(layout).unwrap();
    rstd::mem::memset(first.pointer, u8(0x12), first.size);
    rstd::mem::memset(second.pointer, u8(0x34), second.size);
    auto shrunk = allocator
                      .shrink(first.pointer,
                              layout,
                              rstd::alloc::Layout::from_size_align(usize(4), usize(8)).unwrap())
                      .unwrap();
    EXPECT_EQ(shrunk.pointer, first.pointer);
    EXPECT_EQ(shrunk.size, layout.size);
    EXPECT_EQ(allocator.statistics().used_bytes, usize(16));
    auto grown = allocator.grow(first.pointer, layout, larger).unwrap();
    EXPECT_EQ(grown.pointer, storage + 16);
    EXPECT_EQ(allocator.statistics().used_bytes, usize(32));
    for (auto index = 0; index < 8; ++index) {
        EXPECT_EQ(static_cast<rstd::byte*>(grown.pointer)[index], rstd::byte(0x12));
        EXPECT_EQ(static_cast<rstd::byte*>(second.pointer)[index], rstd::byte(0x34));
    }
}

TEST(FixedBufferAllocator, StrongerAlignmentMayRelocateLastBlock) {
    alignas(64) rstd::byte            storage[129] {};
    rstd::alloc::FixedBufferAllocator allocator(
        mut_ptr<u8[]>::from_raw_parts(storage + 1, usize(128)));
    auto layout  = rstd::alloc::Layout::from_size_align(usize(8), usize(1)).unwrap();
    auto aligned = rstd::alloc::Layout::from_size_align(usize(16), usize(64)).unwrap();
    auto first   = allocator.allocate(layout).unwrap();
    rstd::mem::memset(first.pointer, u8(0x21), first.size);
    auto grown = allocator.grow(first.pointer, layout, aligned).unwrap();
    EXPECT_EQ(grown.pointer, storage + 64);
    for (auto index = 0; index < 8; ++index)
        EXPECT_EQ(static_cast<rstd::byte*>(grown.pointer)[index], rstd::byte(0x21));
    EXPECT_EQ(allocator.statistics().padding_bytes, usize(55));
}

TEST(FixedBufferAllocator, ResetRunsAfterContainersDropAndFreeDoesNotReclaim) {
    alignas(16) rstd::byte            storage[256] {};
    rstd::alloc::FixedBufferAllocator allocator(
        mut_ptr<u8[]>::from_raw_parts(storage, usize(sizeof(storage))));
    {
        auto borrowed = rstd::alloc::allocator_ref(allocator);
        auto values   = alloc::vec::Vec<int, decltype(borrowed)>::make_in(borrowed);
        for (auto value = 0; value < 16; ++value) values.push(int(value));
        EXPECT_EQ(values[usize(15)], 15);
    }
    EXPECT_GT(allocator.statistics().used_bytes, usize());
    allocator.reset();
    EXPECT_EQ(allocator.statistics().used_bytes, usize());
    EXPECT_EQ(allocator.statistics().padding_bytes, usize());
    auto layout = rstd::alloc::Layout::from_size_align(usize(256), usize(1)).unwrap();
    EXPECT_EQ(allocator.allocate(layout).unwrap().pointer, storage);
    allocator.deallocate(storage, layout);
    EXPECT_TRUE(allocator.allocate(rstd::alloc::Layout::make<int>()).is_err());
}

TEST(FixedBufferAllocator, EmptyBufferAcceptsOnlyZeroLayouts) {
    rstd::alloc::FixedBufferAllocator allocator(mut_ptr<u8[]>::from_raw_parts(nullptr, usize()));
    auto zero  = rstd::alloc::Layout::from_size_align(usize(), usize(64)).unwrap();
    auto empty = allocator.allocate(zero).unwrap();
    EXPECT_EQ(reinterpret_cast<uintptr_t>(empty.pointer) % 64, 0u);
    EXPECT_TRUE(allocator.allocate(rstd::alloc::Layout::make<int>()).is_err());
    EXPECT_EQ(allocator.statistics().used_bytes, usize());
}

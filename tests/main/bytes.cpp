#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::bytes::Bytes;
using rstd::bytes::BytesMut;
using rstd::alloc::SharedTrackingAllocator;

static_assert(rstd::bytes::Buf<Bytes>);
static_assert(rstd::bytes::BufMut<BytesMut>);

template<typename A>
concept CanEraseBytesAllocator = requires(Vec<u8, A>&& vec) { Bytes::from_vec(rstd::move(vec)); };
static_assert(! CanEraseBytesAllocator<rstd::alloc::TrackingAllocator<>>);
static_assert(! CanEraseBytesAllocator<rstd::alloc::VirtualMemoryAllocator>);
static_assert(CanEraseBytesAllocator<SharedTrackingAllocator<>>);

TEST(BytesMut, PutSliceAndFreeze) {
    auto buf = BytesMut::with_capacity(usize(8));
    buf.put_slice("hello"_bytes);

    ASSERT_EQ(buf.len(), usize(5));
    EXPECT_EQ(buf[usize()], u8('h'));
    EXPECT_GE(buf.capacity(), usize(8));

    auto frozen = buf.freeze();
    EXPECT_EQ(frozen.len(), usize(5));
    EXPECT_EQ(frozen[usize(4)], u8('o'));
}

TEST(BytesMut, ChunkMutAndAdvanceMut) {
    auto buf = BytesMut::with_capacity(usize(4));

    auto chunk = buf.chunk_mut();
    ASSERT_EQ(chunk.len(), usize(4));
    chunk[usize()]  = u8(1);
    chunk[usize(1)] = u8(2);
    chunk[usize(2)] = u8(3);
    buf.advance_mut(usize(3));

    EXPECT_EQ(buf.len(), usize(3));
    EXPECT_EQ(buf[usize()], u8(1));
    EXPECT_EQ(buf[usize(2)], u8(3));
}

TEST(BytesMut, AppendsAfterWritableChunkAtTheLogicalEnd) {
    auto buf        = BytesMut::with_capacity(usize(4));
    auto chunk      = buf.chunk_mut();
    chunk[usize()]  = u8(1);
    chunk[usize(1)] = u8(2);
    chunk[usize(2)] = u8(3);
    buf.advance_mut(usize(3));

    byte raw[] { byte { 4 }, byte { 5 } };
    buf.put_slice(rstd::as_u8_slice(slice<byte>::from_raw_parts(raw, usize(2))));

    auto frozen = buf.freeze();
    ASSERT_EQ(frozen.len(), usize(5));
    for (rstd::size_t index = 0; index < 5; ++index) {
        EXPECT_EQ(frozen[usize(index)], u8(index + 1));
    }
}

TEST(BytesMut, ResizeFillsLogicalGrowth) {
    auto buf = BytesMut::with_capacity(usize(8));
    buf.put_slice("ab"_bytes);

    buf.resize(usize(6), u8(0x7f));
    ASSERT_EQ(buf.len(), usize(6));
    EXPECT_EQ(buf[usize()], u8('a'));
    EXPECT_EQ(buf[usize(1)], u8('b'));
    for (rstd::size_t index = 2; index < 6; ++index) {
        EXPECT_EQ(buf[usize(index)], u8(0x7f));
    }

    buf.truncate(usize(3));
    buf.resize(usize(5), u8(0x55));
    ASSERT_EQ(buf.len(), usize(5));
    EXPECT_EQ(buf[usize(2)], u8(0x7f));
    EXPECT_EQ(buf[usize(3)], u8(0x55));
    EXPECT_EQ(buf[usize(4)], u8(0x55));
}

TEST(BytesMut, AdvanceAndSplitTo) {
    rstd::byte data[] { byte { 1 }, byte { 2 }, byte { 3 }, byte { 4 }, byte { 5 } };
    auto       buf = BytesMut::make();
    buf.extend_from_slice(slice<u8>::from_raw_parts(data, usize(5)));

    buf.advance(usize(2));
    ASSERT_EQ(buf.len(), usize(3));
    EXPECT_EQ(buf[usize()], u8(3));

    auto prefix = buf.split_to(usize(2));
    EXPECT_EQ(prefix.len(), usize(2));
    EXPECT_EQ(prefix[usize()], u8(3));
    EXPECT_EQ(prefix[usize(1)], u8(4));

    EXPECT_EQ(buf.len(), usize(1));
    EXPECT_EQ(buf[usize()], u8(5));
}

TEST(Bytes, CopyFromSliceAndAdvance) {
    rstd::byte data[] { byte { 9 }, byte { 8 }, byte { 7 } };
    auto bytes = Bytes::copy_from_slice(rstd::slice<rstd::u8>::from_raw_parts(data, usize(3)));

    EXPECT_EQ(bytes.remaining(), usize(3));
    bytes.advance(usize(1));
    EXPECT_EQ(bytes.len(), usize(2));
    EXPECT_EQ(bytes[usize()], u8(8));
}

TEST(Bytes, CopiesLogicalValuesFromPhysicalByteStorage) {
    rstd::byte data[] { byte { 0x61 }, byte {}, byte { 0xff } };
    auto       raw   = slice<rstd::byte>::from_raw_parts(data, usize(3));
    auto       bytes = Bytes::copy_from_slice(rstd::as_u8_slice(raw));

    ASSERT_EQ(bytes.len(), usize(3));
    EXPECT_EQ(bytes[usize()], u8('a'));
    EXPECT_EQ(bytes[usize(1)], u8());
    EXPECT_EQ(bytes[usize(2)], u8(0xff));
}

TEST(ByteSlice, ProjectsPhysicalStorageAsLogicalU8WithoutCopying) {
    byte data[] { byte {}, byte { 127 }, byte { 128 }, byte { 255 } };
    auto values = slice<u8>::from_raw_parts(data, usize(4));
    auto raw    = rstd::as_bytes(values);

    EXPECT_EQ(raw.as_raw_ptr(), data);
    ASSERT_EQ(raw.len(), usize(4));
    EXPECT_EQ(values[usize()], u8());
    EXPECT_EQ(values[usize(1)], u8(127));
    EXPECT_EQ(values[usize(2)], u8(128));
    EXPECT_EQ(values[usize(3)], u8(255));
}

TEST(ByteSlice, MutatesPhysicalStorageThroughLogicalAndRawViews) {
    byte data[] { byte {}, byte {} };
    auto values = mut_ref<u8[]>::from_raw_parts(data, usize(2));
    auto raw    = rstd::as_bytes_mut(values);

    values[usize()] = u8(128);
    raw[usize(1)]   = rstd::byte { 255 };

    EXPECT_EQ(data[0], rstd::byte { 128 });
    EXPECT_EQ(data[1], rstd::byte { 255 });
    EXPECT_EQ(values[usize()], u8(128));
    EXPECT_EQ(values[usize(1)], u8(255));
}

TEST(ByteSlice, LogicalProjectionConvertsElementsToU8) {
    rstd::byte data[] { byte {}, byte { 127 }, byte { 128 }, byte { 255 } };
    auto       raw    = slice<rstd::byte>::from_raw_parts(data, usize(4));
    auto       values = rstd::as_u8_slice(raw);

    ASSERT_EQ(values.len(), usize(4));
    EXPECT_EQ(values[usize()], u8());
    EXPECT_EQ(values[usize(3)], u8(255));
}

TEST(ByteSlice, EmptyLogicalProjectionIsStable) {
    auto values = rstd::as_u8_slice(slice<rstd::byte> {});
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(values.as_raw_ptr(), nullptr);
}

TEST(Hash, RawAndOwnedByteViewsMatch) {
    rstd::byte raw[] { byte {}, byte { 127 }, byte { 128 }, byte { 255 } };
    auto       raw_values = rstd::as_u8_slice(slice<rstd::byte>::from_raw_parts(raw, usize(4)));
    auto       owned      = Vec<u8>::from(raw_values);

    rstd::hash::DefaultHasher raw_hasher;
    raw_hasher.write(raw_values);

    rstd::hash::DefaultHasher owned_hasher;
    owned_hasher.write(owned.as_slice());

    EXPECT_EQ(raw_hasher.finish(), owned_hasher.finish());
}

TEST(Hash, ValueUsesItsRawObjectRepresentation) {
    u32 value(0x12345678U);

    rstd::hash::DefaultHasher value_hasher;
    value_hasher.write_value(value);

    auto raw = slice<rstd::byte>::from_raw_parts(
        reinterpret_cast<rstd::byte const*>(rstd::addressof(value)), usize(sizeof(value)));
    rstd::hash::DefaultHasher raw_hasher;
    raw_hasher.write(rstd::as_u8_slice(raw));

    EXPECT_EQ(value_hasher.finish(), raw_hasher.finish());
}

TEST(BytesAllocator, EmptyConstructionAndCopyUseConfiguredAllocator) {
    auto tracker = SharedTrackingAllocator<>::make();
    {
        auto empty    = Bytes::new_in(tracker.clone());
        auto writable = BytesMut::new_in(tracker.clone());
        EXPECT_TRUE(empty.is_empty());
        EXPECT_TRUE(writable.is_empty());
        EXPECT_EQ(tracker.snapshot().allocations, u64(2));
        writable.put_slice("abc"_bytes);
        auto copied = Bytes::copy_from_slice_in(writable.as_slice(), tracker.clone());
        EXPECT_EQ(copied.as_slice(), "abc"_bytes);
        EXPECT_EQ(tracker.snapshot().allocations, u64(5));
        EXPECT_GT(tracker.snapshot().live_bytes,
                  u64((writable.capacity() + copied.capacity()).to_primitive()));
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
    EXPECT_EQ(tracker.snapshot().deallocations, u64(5));
    EXPECT_FALSE(tracker.snapshot().overflow);
}

TEST(BytesAllocator, VecAndFreezeTransferAllocation) {
    auto tracker = SharedTrackingAllocator<>::make();
    {
        auto vec = Vec<u8, SharedTrackingAllocator<>>::with_capacity_in(usize(8), tracker.clone());
        vec.extend_from_slice("abcdef"_bytes);
        auto* storage = vec.data();
        auto  buffer  = BytesMut::from_vec(rstd::move(vec));
        EXPECT_EQ(buffer.data(), storage);
        EXPECT_EQ(buffer.capacity(), usize(8));
        EXPECT_TRUE(vec.is_empty());
        buffer.advance(usize(2));
        auto frozen = buffer.freeze();
        EXPECT_EQ(frozen.as_slice(), "cdef"_bytes);
        EXPECT_EQ(frozen.data(), storage + 2);
        EXPECT_TRUE(buffer.is_empty());
        EXPECT_EQ(tracker.snapshot().allocations, u64(2));
        EXPECT_EQ(tracker.snapshot().grows, u64());
        auto moved = rstd::move(frozen);
        EXPECT_EQ(moved.as_slice(), "cdef"_bytes);
        EXPECT_TRUE(frozen.is_empty());
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
    EXPECT_EQ(tracker.snapshot().deallocations, u64(2));
}

TEST(BytesAllocator, SplitResizeAndWritableChunkKeepDomain) {
    auto tracker = SharedTrackingAllocator<>::make();
    {
        auto buffer = BytesMut::with_capacity_in(usize(8), tracker.clone());
        buffer.put_slice("abcdef"_bytes);
        buffer.advance(usize(1));
        auto prefix = buffer.split_to(usize(2));
        EXPECT_EQ(prefix.as_slice(), "bc"_bytes);
        EXPECT_EQ(buffer.as_slice(), "def"_bytes);
        buffer.resize(usize(32), u8('x'));
        EXPECT_EQ(buffer[usize()], u8('d'));
        EXPECT_EQ(buffer[usize(31)], u8('x'));
        auto chunk     = buffer.chunk_mut();
        chunk[usize()] = u8('!');
        buffer.advance_mut(usize(1));
        auto tail = buffer.split();
        EXPECT_TRUE(buffer.is_empty());
        EXPECT_EQ(tail.len(), usize(33));
        EXPECT_EQ(tail[usize(32)], u8('!'));
        EXPECT_GT(tracker.snapshot().grows, u64());
        EXPECT_GT(tracker.snapshot().live_bytes,
                  u64((buffer.capacity() + prefix.capacity() + tail.capacity()).to_primitive()));
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
    EXPECT_FALSE(tracker.snapshot().overflow);
}

TEST(BytesAllocator, MoveAssignmentReleasesOldAllocatorDomain) {
    auto first  = SharedTrackingAllocator<>::make();
    auto second = SharedTrackingAllocator<>::make();
    {
        auto source = BytesMut::with_capacity_in(usize(8), first.clone());
        source.put_slice("abcdef"_bytes);
        source.advance(usize(2));
        auto target = BytesMut::with_capacity_in(usize(16), second.clone());
        target      = rstd::move(source);
        EXPECT_TRUE(source.is_empty());
        EXPECT_EQ(target.as_slice(), "cdef"_bytes);
        EXPECT_EQ(second.snapshot().live_bytes, u64());
        auto frozen = target.freeze();
        frozen.advance(usize(1));
        auto other = Bytes::copy_from_slice_in("old"_bytes, second.clone());
        other      = rstd::move(frozen);
        EXPECT_TRUE(frozen.is_empty());
        EXPECT_EQ(other.as_slice(), "def"_bytes);
        EXPECT_EQ(second.snapshot().live_bytes, u64());
        EXPECT_GT(first.snapshot().live_bytes, u64(8));
        source.put_slice("reuse"_bytes);
        EXPECT_EQ(source.as_slice(), "reuse"_bytes);
    }
    EXPECT_EQ(first.snapshot().live_bytes, u64());
    EXPECT_FALSE(first.snapshot().overflow);
    EXPECT_FALSE(second.snapshot().overflow);
}

TEST(BytesAllocator, LastBufferKeepsAllocatorAliveAcrossThreads) {
    auto tracker  = SharedTrackingAllocator<>::make();
    auto observer = tracker.downgrade();
    auto buffer   = BytesMut::with_capacity_in(usize(8), rstd::move(tracker));
    buffer.put_slice("abcdef"_bytes);
    auto prefix = buffer.split_to(usize(2));
    auto frozen = buffer.freeze();
    prefix      = BytesMut::make();
    EXPECT_FALSE(observer.expired());
    auto worker = rstd::thread::spawn([bytes = rstd::move(frozen)] {
                      EXPECT_EQ(bytes.as_slice(), "cdef"_bytes);
                  }).unwrap();
    EXPECT_TRUE(rstd::move(worker).join().is_ok());
    EXPECT_TRUE(observer.expired());
}

TEST(BytesAllocator, GlobalVecAndEmptyReservedVecKeepStorage) {
    auto  vec     = Vec<u8>::with_capacity(usize(32));
    auto* storage = vec.data();
    auto  buffer  = BytesMut::from_vec(rstd::move(vec));
    EXPECT_EQ(buffer.data(), storage);
    EXPECT_EQ(buffer.capacity(), usize(32));
    EXPECT_TRUE(vec.is_empty());
    buffer.put_slice("global"_bytes);
    auto frozen = buffer.freeze();
    EXPECT_EQ(frozen.data(), storage);
    EXPECT_EQ(frozen.as_slice(), "global"_bytes);
    auto empty = Bytes::from_vec(Vec<u8>::make());
    EXPECT_TRUE(empty.is_empty());
}

TEST(BytesAllocator, CloneSharesStorageWithIndependentViews) {
    auto tracker = SharedTrackingAllocator<>::make();
    {
        auto bytes  = Bytes::copy_from_slice_in("abcdef"_bytes, tracker.clone());
        auto before = tracker.snapshot();
        auto copy   = bytes.clone();
        EXPECT_EQ(copy.data(), bytes.data());
        bytes.advance(usize(2));
        copy.truncate(usize(2));
        EXPECT_EQ(bytes.as_slice(), "cdef"_bytes);
        EXPECT_EQ(copy.as_slice(), "ab"_bytes);
        bytes.clear();
        EXPECT_EQ(copy.as_slice(), "ab"_bytes);
        EXPECT_EQ(tracker.snapshot().allocations, before.allocations);
        EXPECT_EQ(tracker.snapshot().live_bytes, before.live_bytes);
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(BytesAllocator, SplitRetainsAllocatorWithoutRetainingOldBuffers) {
    auto tracker = SharedTrackingAllocator<>::make();
    {
        auto buffer = BytesMut::with_capacity_in(usize(8), tracker.clone());
        buffer.put_slice("ab"_bytes);
        auto child = buffer.split_to(usize(1));
        buffer     = BytesMut::make();
        EXPECT_EQ(tracker.snapshot().deallocations, u64(1));
        auto retained = tracker.snapshot().live_bytes;
        for (int i = 0; i < 32; ++i) {
            child = child.split();
            EXPECT_EQ(child.as_slice(), "a"_bytes);
            EXPECT_EQ(tracker.snapshot().live_bytes, retained);
        }
        auto allocator = child.allocator();
        child          = BytesMut::make();
        EXPECT_LT(tracker.snapshot().live_bytes, retained);
        EXPECT_GT(tracker.snapshot().live_bytes, u64());
        auto reused = Bytes::copy_from_slice_in("alive"_bytes, rstd::move(allocator));
        EXPECT_EQ(reused.as_slice(), "alive"_bytes);
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
    EXPECT_EQ(tracker.snapshot().allocations, tracker.snapshot().deallocations);
}

TEST(BytesAllocator, TryIntoMutReusesVisibleRangeWithAllocatorHandleAlive) {
    auto tracker = SharedTrackingAllocator<>::make();
    {
        auto writable = BytesMut::with_capacity_in(usize(16), tracker.clone());
        writable.put_slice("abcdef"_bytes);
        auto bytes = writable.freeze();
        bytes.advance(usize(2));
        bytes.truncate(usize(2));
        auto* pointer = bytes.data();
        auto  owner   = bytes.allocator();
        auto  before  = tracker.snapshot();
        auto  result  = rstd::move(bytes).try_into_mut();
        ASSERT_TRUE(result.is_ok());
        auto buffer = rstd::move(result).unwrap();
        EXPECT_TRUE(bytes.is_empty());
        EXPECT_EQ(buffer.data(), pointer);
        EXPECT_EQ(buffer.as_slice(), "cd"_bytes);
        EXPECT_EQ(buffer.capacity(), usize(14));
        EXPECT_EQ(buffer.remaining_mut(), usize(12));
        EXPECT_EQ(tracker.snapshot().allocations, before.allocations);
        EXPECT_EQ(tracker.snapshot().grows, before.grows);
        buffer.put_slice("!"_bytes);
        EXPECT_EQ(buffer.as_slice(), "cd!"_bytes);
        auto chunk = buffer.chunk_mut();
        EXPECT_EQ(chunk[usize()], u8());
        EXPECT_EQ(chunk[usize(1)], u8());
        chunk[usize()] = u8('?');
        buffer.advance_mut(usize(1));
        EXPECT_EQ(buffer.as_slice(), "cd!?"_bytes);
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(BytesAllocator, TryIntoMutReturnsSharedViewUnchanged) {
    auto tracker  = SharedTrackingAllocator<>::make();
    auto bytes    = Bytes::copy_from_slice_in("abcdef"_bytes, tracker.clone());
    auto snapshot = bytes.clone();
    bytes.advance(usize(1));
    bytes.truncate(usize(3));
    auto* pointer = bytes.data();
    auto  before  = tracker.snapshot();
    auto  result  = rstd::move(bytes).try_into_mut();
    ASSERT_TRUE(result.is_err());
    auto restored = rstd::move(result).unwrap_err();
    EXPECT_TRUE(bytes.is_empty());
    EXPECT_EQ(restored.data(), pointer);
    EXPECT_EQ(restored.as_slice(), "bcd"_bytes);
    EXPECT_EQ(restored.capacity(), usize(5));
    EXPECT_EQ(tracker.snapshot().allocations, before.allocations);
    EXPECT_EQ(tracker.snapshot().grows, before.grows);
    EXPECT_EQ(snapshot.as_slice(), "abcdef"_bytes);
    snapshot      = Bytes::make();
    auto writable = rstd::move(restored).try_into_mut().unwrap();
    EXPECT_EQ(writable.data(), pointer);
    EXPECT_EQ(writable.as_slice(), "bcd"_bytes);
}

TEST(BytesAllocator, IntoMutCopiesOnlyVisibleRangeAndKeepsSnapshot) {
    auto tracker = SharedTrackingAllocator<>::make();
    {
        auto bytes    = Bytes::copy_from_slice_in("abcdef"_bytes, tracker.clone());
        auto snapshot = bytes.clone();
        bytes.advance(usize(2));
        bytes.truncate(usize(2));
        auto* pointer  = bytes.data();
        auto  before   = tracker.snapshot();
        auto  writable = rstd::move(bytes).into_mut();
        EXPECT_TRUE(bytes.is_empty());
        EXPECT_NE(writable.data(), pointer);
        EXPECT_EQ(writable.as_slice(), "cd"_bytes);
        EXPECT_EQ(writable.capacity(), usize(2));
        EXPECT_EQ(writable.remaining_mut(), usize());
        EXPECT_EQ(tracker.snapshot().allocations, before.allocations + u64(2));
        writable[usize()] = u8('x');
        writable.resize(usize(5), u8('!'));
        EXPECT_EQ(writable.as_slice(), "xd!!!"_bytes);
        EXPECT_EQ(snapshot.as_slice(), "abcdef"_bytes);
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
    EXPECT_EQ(tracker.snapshot().allocations, tracker.snapshot().deallocations);
}

TEST(BytesAllocator, IntoMutReusesUniqueStorageWithoutCopying) {
    auto  tracker  = SharedTrackingAllocator<>::make();
    auto  bytes    = Bytes::copy_from_slice_in("abc"_bytes, tracker.clone());
    auto* pointer  = bytes.data();
    auto  before   = tracker.snapshot();
    auto  writable = rstd::move(bytes).into_mut();
    EXPECT_EQ(writable.data(), pointer);
    EXPECT_EQ(tracker.snapshot().allocations, before.allocations);
    writable[usize()] = u8('x');
    EXPECT_EQ(writable.freeze().as_slice(), "xbc"_bytes);
}

TEST(BytesAllocator, EmptyConversionsRetainConfiguredAllocator) {
    auto tracker = SharedTrackingAllocator<>::make();
    {
        auto bytes  = Bytes::new_in(tracker.clone());
        auto before = tracker.snapshot();
        auto unique = rstd::move(bytes).try_into_mut().unwrap();
        EXPECT_TRUE(unique.is_empty());
        EXPECT_EQ(tracker.snapshot().allocations, before.allocations);
        unique.put_slice("a"_bytes);
        EXPECT_EQ(tracker.snapshot().allocations, before.allocations + u64(1));
        auto empty    = unique.freeze();
        auto snapshot = empty.clone();
        empty.advance(usize(1));
        auto shared = rstd::move(empty).into_mut();
        EXPECT_TRUE(shared.is_empty());
        before = tracker.snapshot();
        shared.put_slice("b"_bytes);
        EXPECT_EQ(tracker.snapshot().allocations, before.allocations + u64(1));
        EXPECT_EQ(snapshot.as_slice(), "a"_bytes);
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
    auto default_empty = Bytes::make();
    auto writable      = rstd::move(default_empty).try_into_mut().unwrap();
    EXPECT_TRUE(writable.is_empty());
    writable.put_slice("default"_bytes);
    EXPECT_EQ(writable.as_slice(), "default"_bytes);
}

TEST(BytesMut, TruncatedTailIsNotExposedByWritableChunk) {
    auto buffer = BytesMut::with_capacity(usize(8));
    buffer.put_slice("abcdef"_bytes);
    buffer.advance(usize(2));
    EXPECT_EQ(buffer.capacity(), usize(6));
    EXPECT_EQ(buffer.remaining_mut(), usize(2));
    buffer.truncate(usize(1));
    auto chunk = buffer.chunk_mut();
    ASSERT_EQ(chunk.len(), usize(5));
    for (usize i; i < chunk.len(); ++i) EXPECT_EQ(chunk[i], u8());
    chunk[usize()] = u8('!');
    buffer.advance_mut(usize(1));
    EXPECT_EQ(buffer.as_slice(), "c!"_bytes);
    buffer.resize(usize(1), u8());
    EXPECT_EQ(buffer.chunk_mut()[usize()], u8());
}

TEST(Bytes, TypedVecTransfersNativeBytesAndAlignment) {
    auto values = Vec<float>::with_capacity(usize(8));
    values.push(1.5f);
    values.push(-2.0f);
    float expected[] { 1.5f, -2.0f };
    auto* pointer = reinterpret_cast<const byte*>(values.data());
    auto  bytes   = Bytes::from_vec(rstd::move(values));
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(bytes.data(), pointer);
    EXPECT_EQ(bytes.len(), usize(sizeof(expected)));
    EXPECT_EQ(bytes.capacity(), usize(8 * sizeof(float)));
    EXPECT_EQ(bytes.alignment(), usize(alignof(float)));
    EXPECT_EQ(__builtin_memcmp(bytes.data(), expected, sizeof(expected)), 0);
    bytes.advance(usize(1));
    EXPECT_EQ(bytes.alignment(), usize(1));
    auto snapshot = bytes.clone();
    auto writable = rstd::move(bytes).into_mut();
    EXPECT_EQ(writable.alignment(), usize(alignof(float)));
    EXPECT_EQ(writable.as_slice(), snapshot.as_slice());
    EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(writable.data()) % alignof(float), 0u);
}

TEST(BytesAllocator, DetachGrowAndSplitRetainStorageAlignment) {
    auto tracker = SharedTrackingAllocator<>::make();
    {
        auto values = Vec<u8, SharedTrackingAllocator<>>::with_capacity_aligned_in(
            usize(32), usize(256), tracker.clone());
        values.extend_from_slice("abcdefghijkl"_bytes);
        auto bytes = Bytes::from_vec(rstd::move(values));
        EXPECT_EQ(bytes.alignment(), usize(256));
        bytes.advance(usize(4));
        EXPECT_EQ(bytes.alignment(), usize(4));
        bytes.truncate(usize(4));
        auto snapshot = bytes.clone();
        auto writable = rstd::move(bytes).into_mut();
        EXPECT_EQ(writable.as_slice(), "efgh"_bytes);
        EXPECT_EQ(writable.alignment(), usize(256));
        writable.reserve(usize(1024));
        EXPECT_EQ(writable.alignment(), usize(256));
        EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(writable.data()) % 256, 0u);
        auto prefix = writable.split_to(usize(2));
        EXPECT_EQ(prefix.alignment(), usize(256));
        EXPECT_EQ(prefix.as_slice(), "ef"_bytes);
        EXPECT_EQ(writable.alignment(), usize(2));
        writable.resize(usize(32), u8('!'));
        EXPECT_EQ(writable.alignment(), usize(256));
        EXPECT_EQ(snapshot.as_slice(), "efgh"_bytes);
    }
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
    EXPECT_FALSE(tracker.snapshot().overflow);
}

TEST(Bytes, EmptyTypedVecRetainsAlignmentThroughDetach) {
    auto bytes = Bytes::from_vec(Vec<u32>::make());
    EXPECT_TRUE(bytes.is_empty());
    EXPECT_EQ(bytes.alignment(), usize(alignof(u32)));
    auto snapshot = bytes.clone();
    auto writable = rstd::move(bytes).into_mut();
    EXPECT_EQ(writable.alignment(), usize(alignof(u32)));
    writable.put_slice("a"_bytes);
    EXPECT_EQ(writable.alignment(), usize(alignof(u32)));
    EXPECT_TRUE(snapshot.is_empty());
}

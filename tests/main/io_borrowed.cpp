#include <rstd/test/gtest.hpp>
import rstd;

using rstd::mem::MaybeUninit;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

struct BorrowedTestReader {
    slice<u8>   source;
    usize       position {};
    usize       chunk { 2 };
    usize       calls {};
    usize       initialized_bytes {};
    usize       capacity_seen {};
    const byte* destination {};
    bool        initialize {};
    bool        fail_first {};
    bool        interrupt_first {};
};
template<>
struct rstd::Impl<io::Read, BorrowedTestReader> : DefaultInImpl<io::Read, BorrowedTestReader> {
    auto read_buf(io::BorrowedCursor cursor) -> io::Result<empty> {
        auto& self         = this->self();
        self.capacity_seen = cursor.capacity();
        self.destination   = reinterpret_cast<const byte*>(cursor.as_uninit().as_raw_ptr());
        if (self.initialize) {
            // A backend requiring initialized input writes a marker into unused capacity.
            auto target = cursor.ensure_init();
            for (usize i; i < target.len(); ++i) {
                if (target[i] == u8()) ++self.initialized_bytes;
                target[i] = u8(0xa5);
            }
        }
        auto count = min(min(self.source.len() - self.position, cursor.capacity()), self.chunk);
        if (count != usize())
            cursor.append(slice<u8>::from_raw_parts(
                self.source.as_raw_ptr() + self.position.to_primitive(), count));
        self.position += count;
        if (self.calls++ == usize()) {
            if (self.fail_first)
                return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::PermissionDenied }));
            if (self.interrupt_first)
                return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Interrupted }));
        }
        return Ok(empty {});
    }
};
struct BorrowedTestAt {
    bool          retry {};
    mutable usize calls {};
};
template<>
struct rstd::Impl<io::ReadAt, BorrowedTestAt> : DefaultInImpl<io::ReadAt, BorrowedTestAt> {
    auto read_buf_at(io::BorrowedCursor cursor, u64 offset) const -> io::Result<empty> {
        constexpr auto input = "abcdefgh"_bytes;
        if (offset >= u64(8)) return Ok(empty {});
        auto count = min(cursor.capacity(), usize(8) - usize(offset.to_primitive()));
        if (this->self().retry) count = min(count, usize(2));
        cursor.append(slice<u8>::from_raw_parts(input.as_raw_ptr() + offset.to_primitive(), count));
        if (this->self().retry) {
            if (this->self().calls++ == usize())
                return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Interrupted }));
            return Ok(empty {});
        }
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::PermissionDenied }));
    }
};

TEST(IoBorrowed, CursorBoundsReborrowAndInitialization) {
    rstd::array<MaybeUninit<byte>, 8> bytes;
    auto                              buffer = io::BorrowedBuf::from_uninit(bytes.as_mut_slice());
    EXPECT_EQ(buffer.len(), usize());
    {
        auto cursor = buffer.unfilled();
        EXPECT_FALSE(cursor.advance(usize(1)));
        EXPECT_FALSE(cursor.advance_unchecked(usize(9)));
        {
            auto limited     = cursor.limit(usize(3));
            auto initialized = limited.ensure_init();
            EXPECT_EQ(initialized.len(), usize(3));
            initialized[usize()] = u8('x');
            EXPECT_TRUE(limited.advance(usize(1)));
            EXPECT_EQ(limited.written(), usize(1));
        }
        EXPECT_EQ(buffer.initialized_len(), usize(3));
        EXPECT_EQ(cursor.written(), usize(1));
        {
            auto child = cursor.reborrow();
            EXPECT_EQ(child.written(), usize(1));
            child.append("yz"_bytes);
            EXPECT_EQ(child.written(), usize(3));
        }
        EXPECT_EQ(buffer.filled(), "xyz"_bytes);
        EXPECT_EQ(cursor.capacity(), usize(5));
    }
    buffer.clear();
    EXPECT_EQ(buffer.initialized_len(), usize(3));
    auto next        = buffer.unfilled();
    auto initialized = next.ensure_init();
    EXPECT_EQ(initialized[usize()], u8('x'));
    EXPECT_EQ(initialized[usize(3)], u8());
    EXPECT_EQ(buffer.initialized_len(), usize(8));
}

TEST(IoBorrowed, InitializedStorageIsNotClearedAndZeroCapacityWorks) {
    byte bytes[2] { byte('a'), byte('b') };
    auto buffer = io::BorrowedBuf::from_initialized(mut_ref<u8[]>::from_raw_parts(bytes, usize(2)));
    auto cursor = buffer.unfilled();
    EXPECT_EQ(cursor.ensure_init()[usize(1)], u8('b'));
    EXPECT_TRUE(cursor.advance(usize(2)));
    EXPECT_EQ(buffer.filled(), "ab"_bytes);
    auto zero     = io::BorrowedBuf::from_uninit(mut_ref<MaybeUninit<byte>[]>());
    auto no_space = zero.unfilled();
    EXPECT_TRUE(no_space.advance(usize()));
    EXPECT_TRUE(no_space.ensure_init().is_empty());
    no_space.append(""_bytes);
}

TEST(IoBorrowed, ReadToEndPublishesPartialErrorAndInterruptedProgress) {
    auto               output = Vec<u8>::from("pre"_bytes);
    BorrowedTestReader reader { .source = "abcdef"_bytes, .fail_first = true };
    auto               failed = io::read_to_end(reader, output);
    ASSERT_TRUE(failed.is_err());
    EXPECT_EQ(output.as_slice(), "preab"_bytes);
    EXPECT_EQ(reader.position, usize(2));
    EXPECT_EQ(io::read_to_end(reader, output).unwrap(), usize(4));
    EXPECT_EQ(output.as_slice(), "preabcdef"_bytes);
    BorrowedTestReader interrupted { .source = "123456"_bytes, .interrupt_first = true };
    output.clear();
    EXPECT_EQ(io::read_to_end(interrupted, output).unwrap(), usize(6));
    EXPECT_EQ(output.as_slice(), "123456"_bytes);
}

TEST(IoBorrowed, ReadExactAndCopyKeepProgressAcrossInterrupted) {
    rstd::array<MaybeUninit<byte>, 6> bytes;
    auto                              buffer = io::BorrowedBuf::from_uninit(bytes.as_mut_slice());
    BorrowedTestReader                reader { .source = "abcdef"_bytes, .interrupt_first = true };
    EXPECT_TRUE(io::read_buf_exact(reader, buffer.unfilled()).is_ok());
    EXPECT_EQ(buffer.filled(), "abcdef"_bytes);
    BorrowedTestReader failing { .source = "123456"_bytes, .fail_first = true };
    auto               writer = io::Cursor(Vec<u8>::make());
    EXPECT_TRUE(io::copy(failing, writer).is_err());
    EXPECT_EQ(writer.get_ref().as_slice(), "12"_bytes);
    BorrowedTestReader retry { .source = "xyz"_bytes, .interrupt_first = true };
    EXPECT_EQ(io::copy(retry, writer).unwrap(), u64(3));
    EXPECT_EQ(writer.get_ref().as_slice(), "12xyz"_bytes);
}

TEST(IoBorrowed, InitializedShortReadsReuseSpareState) {
    auto               output = Vec<u8>::with_capacity(usize(64));
    BorrowedTestReader reader { .source = "abcdefgh"_bytes, .initialize = true };
    EXPECT_EQ(io::read_to_end(reader, output).unwrap(), usize(8));
    EXPECT_EQ(reader.initialized_bytes, usize(64));
    EXPECT_EQ(output.as_slice(), "abcdefgh"_bytes);
}

TEST(IoBorrowed, BufReaderReusesInitializationAndBypassesLargeReads) {
    auto reader = io::BufReader(
        BorrowedTestReader { .source = "abcdefgh"_bytes, .initialize = true }, usize(8));
    for (int i = 0; i < 4; ++i) {
        auto bytes = as<io::BufRead>(reader).fill_buf().unwrap();
        EXPECT_EQ(bytes.len(), usize(2));
        as<io::BufRead>(reader).consume(usize(2));
    }
    EXPECT_EQ(reader.get_ref().initialized_bytes, usize(8));
    auto large = io::BufReader(BorrowedTestReader { .source = "abcdefgh"_bytes, .chunk = usize(8) },
                               usize(4));
    rstd::array<MaybeUninit<byte>, 8> bytes;
    auto                              buffer = io::BorrowedBuf::from_uninit(bytes.as_mut_slice());
    as<io::Read>(large).read_buf(buffer.unfilled()).unwrap();
    EXPECT_EQ(large.get_ref().destination, bytes.as_ptr().as_byte_ptr());
    EXPECT_TRUE(large.buffer().is_empty());
    EXPECT_EQ(buffer.filled(), "abcdefgh"_bytes);
}

TEST(IoBorrowed, BufReaderRetainsRemainderOfPartialError) {
    auto reader = io::BufReader(
        BorrowedTestReader { .source = "abcdef"_bytes, .chunk = usize(4), .fail_first = true },
        usize(8));
    rstd::array<MaybeUninit<byte>, 2> bytes;
    auto                              buffer = io::BorrowedBuf::from_uninit(bytes.as_mut_slice());
    EXPECT_TRUE(as<io::Read>(reader).read_buf(buffer.unfilled()).is_err());
    EXPECT_EQ(buffer.filled(), "ab"_bytes);
    EXPECT_EQ(reader.buffer(), "cd"_bytes);
    auto output = Vec<u8>::make();
    EXPECT_EQ(io::read_to_end(reader, output).unwrap(), usize(4));
    EXPECT_EQ(output.as_slice(), "cdef"_bytes);
}

TEST(IoBorrowed, ReadExactAtRetriesPartialProgressAtCorrectOffset) {
    BorrowedTestAt source { .retry = true };
    byte           bytes[5] {};
    io::read_exact_at(source, mut_ref<u8[]>::from_raw_parts(bytes, usize(5)), u64(1)).unwrap();
    EXPECT_EQ(slice<u8>::from_raw_parts(bytes, usize(5)), "bcdef"_bytes);
}

TEST(IoBorrowed, RangeAndSharedReadAtAccountForPartialError) {
    auto source = io::SharedReadAt::make(BorrowedTestAt {});
    auto range  = io::RangeReader::make(rstd::move(source), u64(2), u64(3)).unwrap();
    rstd::array<MaybeUninit<byte>, 8> bytes;
    auto                              buffer = io::BorrowedBuf::from_uninit(bytes.as_mut_slice());
    buffer.unfilled().append("!"_bytes);
    EXPECT_TRUE(range.read_buf(buffer.unfilled()).is_err());
    EXPECT_EQ(buffer.filled(), "!cde"_bytes);
    EXPECT_EQ(range.position(), u64(3));
    buffer.clear();
    EXPECT_TRUE(range.read_buf(buffer.unfilled()).is_ok());
    EXPECT_EQ(buffer.len(), usize());
}

TEST(IoBorrowed, DynReadWritesOnlyFilledPrefix) {
    auto handle =
        Box<dyn<io::Read>>::make(BorrowedTestReader { .source = "abc"_bytes, .chunk = usize(3) });
    rstd::array<MaybeUninit<byte>, 32> bytes;
    mem::memset(bytes.as_mut_ptr().as_byte_ptr(), u8(0xa5), usize(32));
    auto buffer = io::BorrowedBuf::from_uninit(bytes.as_mut_slice());
    handle->read_buf(buffer.unfilled()).unwrap();
    EXPECT_EQ(buffer.filled(), "abc"_bytes);
    EXPECT_EQ(buffer.initialized_len(), usize(3));
    EXPECT_EQ(bytes.as_ptr().as_byte_ptr()[31], byte(0xa5));
}

TEST(IoBorrowed, SizeHintsRemainHints) {
    const usize hints[] { usize(), usize(1), usize(64) };
    for (auto hint : hints) {
        auto               output = Vec<u8>::make();
        BorrowedTestReader reader { .source = "abcdef"_bytes };
        EXPECT_EQ(io::default_read_to_end(reader, output, Some(hint)).unwrap(), usize(6));
        EXPECT_EQ(output.as_slice(), "abcdef"_bytes);
    }
}

TEST(IoBorrowed, FileHintUsesCurrentPositionAndPositionalReadKeepsIt) {
    auto directory = fs::TempDir::make("rstd-borrowed-file"_str).unwrap();
    auto root      = path::PathBuf::from(directory.path().as_os_str().to_os_string());
    auto path      = root.join("input"_str);
    fs::write(path.as_path(), "abcdef"_bytes).unwrap();
    auto file = fs::File::open(path.as_path()).unwrap();
    file.seek(io::SeekFrom::from_start(u64(2))).unwrap();
    EXPECT_EQ(file.read_size_hint().unwrap(), usize(4));
    rstd::array<MaybeUninit<byte>, 2> bytes;
    auto                              buffer = io::BorrowedBuf::from_uninit(bytes.as_mut_slice());
    file.read_buf_at(buffer.unfilled(), u64(4)).unwrap();
    EXPECT_EQ(buffer.filled(), "ef"_bytes);
    EXPECT_EQ(io::stream_position(file).unwrap(), u64(2));
    buffer.clear();
    file.read_buf_at(buffer.unfilled(), u64(64)).unwrap();
    EXPECT_EQ(buffer.len(), usize());
    auto shared = io::SharedReadAt::make(file.try_clone().unwrap());
    auto range  = io::RangeReader::make(rstd::move(shared), u64(1), u64(2)).unwrap();
    range.read_buf(buffer.unfilled()).unwrap();
    EXPECT_EQ(buffer.filled(), "bc"_bytes);
    EXPECT_EQ(io::stream_position(file).unwrap(), u64(2));
    auto output = Vec<u8>::make();
    EXPECT_EQ(io::read_to_end(file, output).unwrap(), usize(4));
    EXPECT_EQ(output.as_slice(), "cdef"_bytes);
    EXPECT_EQ(file.read_size_hint().unwrap(), usize());
}

template<typename T>
concept BorrowedUninitStorage =
    requires(mut_ref<T[]> storage) { io::BorrowedBuf::from_uninit(storage); };
static_assert(BorrowedUninitStorage<MaybeUninit<byte>>);
static_assert(! BorrowedUninitStorage<byte>);
static_assert(! BorrowedUninitStorage<MaybeUninit<u8>>);

static_assert(mtp::same_as<decltype(mtp::declval<io::BorrowedCursor&>().as_uninit()),
                           mut_ref<MaybeUninit<byte>[]>>);
static_assert(mtp::same_as<decltype(mtp::declval<Vec<u8>&>().spare_capacity_mut().as_uninit()),
                           mut_ref<MaybeUninit<byte>[]>>);

TEST(IoBorrowed, CursorWritesThroughTypedSlots) {
    rstd::array<MaybeUninit<byte>, 4> storage;
    auto                              buffer = io::BorrowedBuf::from_uninit(storage.as_mut_slice());
    auto                              cursor = buffer.unfilled();
    {
        auto slots = cursor.as_uninit();
        slots[usize()].write(byte('a'));
        slots[usize(1)].write(byte('b'));
    }
    EXPECT_FALSE(cursor.advance(usize(2)));
    EXPECT_TRUE(cursor.advance_unchecked(usize(2)));
    EXPECT_EQ(buffer.filled(), "ab"_bytes);
    auto tail = cursor.as_uninit();
    EXPECT_EQ(tail.len(), usize(2));
    tail[usize()].write(byte('c'));
    EXPECT_TRUE(cursor.advance_unchecked(usize(1)));
    EXPECT_EQ(buffer.filled(), "abc"_bytes);
}

TEST(IoBorrowed, SpareCapacityExposesMaybeUninitSlots) {
    auto output = Vec<u8>::from("pre"_bytes);
    output.reserve(usize(8));
    auto spare = output.spare_capacity_mut().as_uninit();
    {
        auto buffer = io::BorrowedBuf::from_uninit(spare);
        EXPECT_EQ(buffer.unfilled().as_uninit().as_raw_ptr(), spare.as_raw_ptr());
        buffer.unfilled().append("abc"_bytes);
        EXPECT_EQ(buffer.filled(), "abc"_bytes);
        EXPECT_EQ(output.as_slice(), "pre"_bytes);
        output.set_len_unchecked(output.len() + buffer.len());
    }
    EXPECT_EQ(output.as_slice(), "preabc"_bytes);
}

TEST(IoBorrowed, RepeatInitializesTypedStorageAndHonorsLimit) {
    rstd::array<MaybeUninit<byte>, 5> storage;
    storage[usize(4)].write(byte('!'));
    auto buffer = io::BorrowedBuf::from_uninit(storage.as_mut_slice());
    auto reader = io::repeat(u8('x'));
    auto cursor = buffer.unfilled();
    EXPECT_TRUE(as<io::Read>(reader).read_buf(cursor.limit(usize(4))).is_ok());
    EXPECT_EQ(buffer.filled(), "xxxx"_bytes);
    EXPECT_EQ(buffer.initialized_len(), usize(4));
    EXPECT_EQ(storage[usize(4)].assume_init_read(), byte('!'));
}

#if __has_feature(memory_sanitizer)
extern "C" rstd::intptr_t __msan_test_shadow(const volatile void*, rstd::size_t);

TEST(IoBorrowed, TypedStorageOnlyInitializesWrittenOrEnsuredPrefix) {
    rstd::array<MaybeUninit<byte>, 8> storage;
    auto                              buffer = io::BorrowedBuf::from_uninit(storage.as_mut_slice());
    auto                              cursor = buffer.unfilled();
    EXPECT_EQ(__msan_test_shadow(storage.data(), sizeof(storage)), rstd::intptr_t(0));
    cursor.append("abc"_bytes);
    EXPECT_EQ(buffer.filled(), "abc"_bytes);
    EXPECT_EQ(__msan_test_shadow(storage.data(), sizeof(storage)), rstd::intptr_t(3));
    {
        auto limited     = cursor.limit(usize(2));
        auto initialized = limited.ensure_init();
        EXPECT_EQ(initialized[usize()], u8());
        EXPECT_EQ(initialized[usize(1)], u8());
        EXPECT_EQ(__msan_test_shadow(storage.data(), sizeof(storage)), rstd::intptr_t(5));
        EXPECT_TRUE(limited.advance(usize(2)));
    }
    EXPECT_EQ(buffer.initialized_len(), usize(5));
    buffer.clear();
    EXPECT_EQ(__msan_test_shadow(storage.data(), sizeof(storage)), rstd::intptr_t(5));
    auto again = buffer.unfilled();
    EXPECT_TRUE(again.advance(usize(3)));
    EXPECT_EQ(buffer.filled(), "abc"_bytes);
}

struct UninitializedIoReader {
    bool read = false;
};
template<>
struct rstd::Impl<io::Read, UninitializedIoReader>
    : DefaultInImpl<io::Read, UninitializedIoReader> {
    auto read_buf(io::BorrowedCursor cursor) -> io::Result<empty> {
        if (this->self().read) return Ok(empty {});
        auto bytes = cursor.as_uninit();
        EXPECT_EQ(__msan_test_shadow(bytes.as_raw_ptr(), bytes.len().to_primitive()),
                  rstd::intptr_t(0));
        cursor.append("abc"_bytes);
        this->self().read = true;
        return Ok(empty {});
    }
};

TEST(IoBorrowed, ReaderBufferCopyAndVecPreserveUninitializedCapacity) {
    auto buffered = io::BufReader(UninitializedIoReader {}, usize(8));
    EXPECT_EQ(as<io::BufRead>(buffered).fill_buf().unwrap(), "abc"_bytes);
    UninitializedIoReader copy_source;
    auto                  destination = io::Cursor(Vec<u8>::make());
    EXPECT_EQ(io::copy(copy_source, destination).unwrap(), u64(3));
    EXPECT_EQ(destination.get_ref().as_slice(), "abc"_bytes);
    UninitializedIoReader collect_source;
    auto                  output = Vec<u8>::make();
    EXPECT_EQ(io::read_to_end(collect_source, output).unwrap(), usize(3));
    EXPECT_EQ(output.as_slice(), "abc"_bytes);
}
#endif

struct InitializedOnlyReader {
    usize zeroes {};
    bool  fail {};
    bool  excessive {};
};
template<>
struct rstd::Impl<io::Read, InitializedOnlyReader>
    : DefaultInImpl<io::Read, InitializedOnlyReader> {
    auto read(mut_ref<u8[]> bytes) -> io::Result<usize> {
        auto& self = this->self();
        for (usize i; i < bytes.len(); ++i) {
            if (bytes[i] == u8()) ++self.zeroes;
            bytes[i] = u8('x');
        }
        if (self.fail)
            return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::PermissionDenied }));
        return Ok(self.excessive ? bytes.len() + usize(1) : min(bytes.len(), usize(1)));
    }
};

struct InClassReadOnly : DefaultInClass<InClassReadOnly, io::Read> {
    auto read(mut_ref<u8[]> bytes) -> io::Result<usize> {
        if (bytes.is_empty()) return Ok(usize());
        bytes[usize()] = u8('r');
        return Ok(usize(1));
    }
};
struct InClassReadBufOnly : DefaultInClass<InClassReadBufOnly, io::Read> {
    auto read_buf(io::BorrowedCursor cursor) -> io::Result<empty> {
        if (cursor.capacity() != usize()) cursor.append("b"_bytes);
        return Ok(empty {});
    }
};
static_assert(Impled<InitializedOnlyReader, io::Read>);
static_assert(Impled<InClassReadOnly, io::Read>);
static_assert(Impled<InClassReadBufOnly, io::Read>);

TEST(IoBorrowed, ReadOnlyDefaultInitializesOnceAndPreservesExistingBytes) {
    rstd::array<MaybeUninit<byte>, 4> storage;
    storage[usize()].write(byte('!'));
    auto buffer = io::BorrowedBuf::from_uninit(storage.as_mut_slice());
    buffer.set_init_unchecked(usize(1));
    InitializedOnlyReader reader;
    as<io::Read>(reader).read_buf(buffer.unfilled()).unwrap();
    EXPECT_EQ(reader.zeroes, usize(3));
    EXPECT_EQ(buffer.initialized_len(), usize(4));
    EXPECT_EQ(buffer.filled(), "x"_bytes);
    buffer.clear();
    as<io::Read>(reader).read_buf(buffer.unfilled()).unwrap();
    EXPECT_EQ(reader.zeroes, usize(3));
    EXPECT_EQ(buffer.len(), usize(1));
    byte existing[2] { byte('a'), byte('b') };
    auto initialized =
        io::BorrowedBuf::from_initialized(mut_ref<u8[]>::from_raw_parts(existing, usize(2)));
    as<io::Read>(reader).read_buf(initialized.unfilled()).unwrap();
    EXPECT_EQ(reader.zeroes, usize(3));
}

TEST(IoBorrowed, ReadOnlyDefaultRejectsExcessiveCountsAndDoesNotAdvanceOnError) {
    rstd::array<MaybeUninit<byte>, 4> storage;
    auto                              buffer = io::BorrowedBuf::from_uninit(storage.as_mut_slice());
    buffer.unfilled().append("!"_bytes);
    InitializedOnlyReader reader { .fail = true };
    auto                  result = as<io::Read>(reader).read_buf(buffer.unfilled());
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::PermissionDenied });
    EXPECT_EQ(buffer.filled(), "!"_bytes);
    EXPECT_EQ(buffer.initialized_len(), usize(4));
    reader.fail      = false;
    reader.excessive = true;
    result           = as<io::Read>(reader).read_buf(buffer.unfilled());
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::InvalidData });
    EXPECT_EQ(buffer.filled(), "!"_bytes);
}

TEST(IoBorrowed, InClassDefaultsSupportEitherEntryPoint) {
    rstd::array<MaybeUninit<byte>, 2> storage;
    auto                              buffer = io::BorrowedBuf::from_uninit(storage.as_mut_slice());
    InClassReadOnly                   reader;
    as<io::Read>(reader).read_buf(buffer.unfilled()).unwrap();
    EXPECT_EQ(buffer.filled(), "r"_bytes);
    byte               bytes[2] { byte('!'), byte('!') };
    InClassReadBufOnly buffered;
    EXPECT_EQ(as<io::Read>(buffered).read(mut_ref<u8[]>::from_raw_parts(bytes, usize(2))).unwrap(),
              usize(1));
    EXPECT_EQ(bytes[0], byte('b'));
    EXPECT_EQ(bytes[1], byte('!'));
}

struct DualEntryReader : DefaultInClass<DualEntryReader, io::Read> {
    usize read_calls {}, read_buf_calls {};
    auto  read(mut_ref<u8[]> bytes) -> io::Result<usize> {
        ++read_calls;
        if (bytes.is_empty()) return Ok(usize());
        bytes[usize()] = u8('r');
        return Ok(usize(1));
    }
    auto read_buf(io::BorrowedCursor cursor) -> io::Result<empty> {
        ++read_buf_calls;
        if (cursor.capacity() != usize()) cursor.append("b"_bytes);
        return Ok(empty {});
    }
};
template<>
struct rstd::Impl<io::Seek, DualEntryReader> : ImplBase<DualEntryReader> {
    auto seek(io::SeekFrom) -> io::Result<u64> { return Ok(u64()); }
};
template<>
struct rstd::Impl<io::Write, DualEntryReader> : ImplBase<DualEntryReader> {
    auto write(slice<u8> bytes) -> io::Result<usize> { return Ok(bytes.len()); }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

template<typename Trait>
void check_dual_read_dispatch() {
    auto reader = Box<dyn<Trait>>::make(DualEntryReader {});
    byte bytes[1] {};
    EXPECT_EQ(reader->read(mut_ref<u8[]>::from_raw_parts(bytes, usize(1))).unwrap(), usize(1));
    EXPECT_EQ(bytes[0], byte('r'));
    rstd::array<MaybeUninit<byte>, 1> storage;
    auto                              buffer = io::BorrowedBuf::from_uninit(storage.as_mut_slice());
    reader->read_buf(buffer.unfilled()).unwrap();
    EXPECT_EQ(buffer.filled(), "b"_bytes);
}

TEST(IoBorrowed, DynamicReadAndCompositionsKeepBothEntryPoints) {
    check_dual_read_dispatch<io::Read>();
    check_dual_read_dispatch<io::ReadSeek>();
    check_dual_read_dispatch<io::ReadWriteSeek>();
}

TEST(IoBorrowed, BufReaderOrdinaryReadUsesInnerReadWhenBypassing) {
    auto reader = io::BufReader(DualEntryReader {}, usize(2));
    byte bytes[4] {};
    EXPECT_EQ(as<io::Read>(reader).read(mut_ref<u8[]>::from_raw_parts(bytes, usize(4))).unwrap(),
              usize(1));
    EXPECT_EQ(bytes[0], byte('r'));
    EXPECT_EQ(reader.get_ref().read_calls, usize(1));
    EXPECT_EQ(reader.get_ref().read_buf_calls, usize());
    EXPECT_EQ(as<io::Read>(reader).read(mut_ref<u8[]>::from_raw_parts(bytes, usize(1))).unwrap(),
              usize(1));
    EXPECT_EQ(bytes[0], byte('b'));
    EXPECT_EQ(reader.get_ref().read_buf_calls, usize(1));
}

TEST(IoBorrowed, FileCopyAndInitializedReadsPreservePositionAndTail) {
    auto directory = fs::TempDir::make("rstd-dual-read"_str).unwrap();
    auto root      = path::PathBuf::from(directory.path().as_os_str().to_os_string());
    auto source    = root.join("source"_str);
    auto target    = root.join("target"_str);
    fs::write(source.as_path(), "abcdef"_bytes).unwrap();
    EXPECT_EQ(fs::copy(source.as_path(), target.as_path()).unwrap(), u64(6));
    auto file = fs::File::open(target.as_path()).unwrap();
    byte bytes[8];
    mem::memset(bytes, u8('!'), usize(8));
    auto destination = mut_ref<u8[]>::from_raw_parts(bytes, usize(8));
    EXPECT_EQ(as<io::Read>(file).read(destination).unwrap(), usize(6));
    EXPECT_EQ(slice<u8>::from_raw_parts(bytes, usize(8)), "abcdef!!"_bytes);
    EXPECT_EQ(as<io::ReadAt>(file).read_at(destination, u64(4)).unwrap(), usize(2));
    EXPECT_EQ(slice<u8>::from_raw_parts(bytes, usize(8)), "efcdef!!"_bytes);
    EXPECT_EQ(io::stream_position(file).unwrap(), u64(6));
    auto range =
        io::RangeReader::make(io::SharedReadAt::make(file.try_clone().unwrap()), u64(1), u64(3))
            .unwrap();
    EXPECT_EQ(as<io::Read>(range).read(destination).unwrap(), usize(3));
    EXPECT_EQ(range.position(), u64(3));
    EXPECT_EQ(slice<u8>::from_raw_parts(bytes, usize(8)), "bcddef!!"_bytes);
    EXPECT_EQ(io::stream_position(file).unwrap(), u64(6));
}

TEST(IoBorrowed, ReadExactUsesOrdinaryReadAndChecksReturnedCounts) {
    DualEntryReader reader;
    byte            bytes[3] {};
    auto            output = mut_ref<u8[]>::from_raw_parts(bytes, usize(3));
    io::read_exact(reader, output).unwrap();
    EXPECT_EQ(slice<u8>::from_raw_parts(bytes, usize(3)), "rrr"_bytes);
    EXPECT_EQ(reader.read_calls, usize(3));
    EXPECT_EQ(reader.read_buf_calls, usize());
    InitializedOnlyReader excessive { .excessive = true };
    auto                  result = io::read_exact(excessive, output);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::InvalidData });
}

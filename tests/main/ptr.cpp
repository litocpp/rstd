#include <rstd/test/gtest.hpp>
import rstd;

using rstd::mem::MaybeUninit;

using namespace rstd::prelude;
using namespace rstd::literals;

constexpr bool swap_byte_storage() {
    rstd::byte data[]  = { rstd::byte { 3 }, rstd::byte { 1 } };
    auto       pointer = rstd::mut_ptr<u8>::from_raw_parts(data);
    rstd::ptr_::swap(pointer, pointer.add(1_usize));
    rstd::ptr_::swap(pointer, pointer);
    return data[0] == rstd::byte { 1 } && data[1] == rstd::byte { 3 };
}
static_assert(swap_byte_storage());

TEST(Ptr, SwapLogicalBytes) {
    EXPECT_TRUE(swap_byte_storage());
    rstd::ptr_::swap(rstd::mut_ptr<u8> {}, rstd::mut_ptr<u8> {});
}

TEST(Ptr, CopyNonoverlappingUsesElementCount) {
    struct Pair {
        int first;
        int second;
    };

    Pair source[] { { 1, 2 }, { 3, 4 } };
    Pair destination[] { {}, {} };
    rstd::ptr_::copy_nonoverlapping(rstd::ptr<Pair>::from_raw_parts(source),
                                    rstd::mut_ptr<Pair>::from_raw_parts(destination),
                                    usize(2));

    EXPECT_EQ(destination[0].first, 1);
    EXPECT_EQ(destination[0].second, 2);
    EXPECT_EQ(destination[1].first, 3);
    EXPECT_EQ(destination[1].second, 4);
}

TEST(Ptr, CopyNonoverlappingAcceptsEmptyNullRange) {
    rstd::ptr_::copy_nonoverlapping(rstd::ptr<int>::from_raw_parts(nullptr),
                                    rstd::mut_ptr<int>::from_raw_parts(nullptr),
                                    usize());
}

struct alignas(64) ByteStorageValue {
    int* drops;
    int  value;
    ByteStorageValue(int& drops, int value): drops(&drops), value(value) {}
    ByteStorageValue(ByteStorageValue&& other) noexcept: drops(other.drops), value(other.value) {}
    ~ByteStorageValue() { ++*drops; }
    static void* operator new(rstd::size_t) = delete;
};

TEST(Ptr, MaybeUninitByteStorageUsesElementStride) {
    using Slot = MaybeUninit<ByteStorageValue>;
    static_assert(sizeof(Slot) == sizeof(ByteStorageValue));
    static_assert(alignof(Slot) == alignof(ByteStorageValue));
    static_assert(rstd::mtp::same_as<decltype(mut_ptr<Slot> {}.p), rstd::byte*>);
    static_assert(rstd::mtp::same_as<decltype(mut_ref<Slot[]> {}.p), rstd::byte*>);
    alignas(ByteStorageValue) rstd::byte storage[sizeof(ByteStorageValue) * 3];
    auto                                 pointer = mut_ptr<Slot>::from_bytes(storage);
    EXPECT_EQ(pointer.add(2_usize).as_byte_ptr(), storage + sizeof(ByteStorageValue) * 2);
    EXPECT_EQ(pointer.distance_to(pointer.add(3_usize)), 3);
    EXPECT_EQ(pointer.add(2_usize).offset(rstd::isize(-1)).as_byte_ptr(),
              storage + sizeof(ByteStorageValue));
    EXPECT_EQ(pointer.add(2_usize).sub(2_usize).as_byte_ptr(), storage);
    auto next = pointer;
    ++next;
    --next;
    EXPECT_EQ(next.as_byte_ptr(), storage);
    auto slots = mut_ref<Slot[]>::from_bytes(storage, 3_usize);
    EXPECT_EQ(slots.end().as_byte_ptr(), storage + sizeof(storage));
    int drops = 0;
    for (usize index; index < slots.len(); ++index) {
        rstd::construct_at(slots[index].as_mut_ptr(), drops, int(index.to_primitive()));
    }
    auto read  = slots.as_ref();
    auto parts = rstd::slice_::split_at_mut(slots, 1_usize);
    EXPECT_EQ(parts.get<1>().as_byte_ptr(), storage + sizeof(ByteStorageValue));
    EXPECT_EQ(parts.get<1>()[1_usize].assume_init_ref().value, 2);
    auto chunks = rstd::slice_::chunks(read, 2_usize);
    EXPECT_EQ(chunks.next_back().unwrap()[0_usize].assume_init_ref().value, 2);
    auto iterator = rstd::iter::from_slice(read);
    EXPECT_EQ(iterator.len(), 3_usize);
    EXPECT_EQ(iterator.next_back().unwrap().get().assume_init_ref().value, 2);
    auto mutable_iterator = rstd::as<rstd::iter::IntoIterator>(slots).into_iter();
    EXPECT_EQ(mutable_iterator.next().unwrap().get_mut().assume_init_ref().value, 0);
    EXPECT_EQ(read[2_usize].assume_init_ref().value, 2);
    EXPECT_EQ(slots.last_mut().unwrap().get_mut().assume_init_mut().value, 2);
    for (usize index; index < slots.len(); ++index) slots[index].assume_init_drop();
    EXPECT_EQ(drops, 3);
    rstd::construct_at(pointer->as_mut_ptr(), drops, 42);
    EXPECT_EQ(pointer->assume_init_ref().value, 42);
    pointer->assume_init_drop();
    EXPECT_EQ(drops, 4);
}

TEST(Ptr, MaybeUninitEmptyByteStorageAndSingleStackSlot) {
    using Slot   = MaybeUninit<int>;
    auto pointer = mut_ptr<Slot>::from_bytes(nullptr);
    EXPECT_EQ(pointer.distance_to(pointer), 0);
    EXPECT_EQ(pointer.add(0_usize).as_byte_ptr(), nullptr);
    EXPECT_EQ(pointer.sub(0_usize).as_byte_ptr(), nullptr);
    EXPECT_EQ(pointer.offset(rstd::isize()).as_byte_ptr(), nullptr);
    auto slots = mut_ref<Slot[]>::from_bytes(nullptr, 0_usize);
    EXPECT_TRUE(slots.is_empty());
    EXPECT_EQ(slots.begin().as_byte_ptr(), slots.end().as_byte_ptr());
    Slot stack;
    auto bytes = reinterpret_cast<rstd::byte*>(&stack);
    EXPECT_EQ(reinterpret_cast<rstd::byte*>(stack.as_mut_ptr()), bytes);
    mut_ptr<Slot>::from_bytes(bytes)->write(17);
    EXPECT_EQ(stack.assume_init_ref(), 17);
    stack.assume_init_drop();
}

TEST(Ptr, BoxConstructsInAlignedByteAllocationWithoutTypedNew) {
    int drops = 0;
    {
        auto owner = rstd::boxed::Box<ByteStorageValue>::make(drops, 19);
        EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(owner.get()) % alignof(ByteStorageValue), 0u);
        EXPECT_EQ(owner->value, 19);
    }
    EXPECT_EQ(drops, 1);
}

TEST(Ptr, VecByteStoragePreservesAlignmentAndMoveCount) {
    int drops = 0;
    {
        auto values = rstd::vec::Vec<ByteStorageValue>::with_capacity(2_usize);
        auto spare  = values.spare_capacity_mut();
        spare[0_usize].write(drops, 10);
        spare[1_usize].write(drops, 20);
        values.set_len_unchecked(2_usize);
        EXPECT_EQ(drops, 0);
        values.reserve(8_usize);
        EXPECT_EQ(drops, 2);
        EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(values.data()) % alignof(ByteStorageValue), 0u);
        EXPECT_EQ(values[0_usize].value, 10);
        EXPECT_EQ(values[1_usize].value, 20);
    }
    EXPECT_EQ(drops, 4);
}

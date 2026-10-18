#include <rstd/test/gtest.hpp>

import rstd;

using rstd::mem::MaybeUninit;

using namespace rstd::prelude;

namespace
{

struct NoDefault {
    NoDefault() = delete;
};

struct CloneOnly {
    int value;

    explicit CloneOnly(int value): value(value) {}

    CloneOnly(const CloneOnly&)                    = delete;
    auto operator=(const CloneOnly&) -> CloneOnly& = delete;
    CloneOnly(CloneOnly&&)                         = default;
    auto operator=(CloneOnly&&) -> CloneOnly&      = default;

    auto clone() const -> CloneOnly { return CloneOnly { value }; }
    void clone_from(const CloneOnly& source) { *this = source.clone(); }
};

} // namespace

static_assert(sizeof(rstd::array<int, 4>) == sizeof(int) * 4);
static_assert(rstd::Impled<rstd::array<int, 3>, rstd::clone::Clone>);
static_assert(rstd::Impled<rstd::array<int, 3>, rstd::iter::IntoIterator>);
static_assert(rstd::Impled<rstd::array<int, 3>, rstd::cmp::PartialEq<rstd::array<int, 3>>>);
static_assert(rstd::Impled<rstd::array<int, 3>, rstd::convert::AsRef<int[]>>);
static_assert(rstd::Impled<rstd::array<int, 3>, rstd::convert::AsMut<int[]>>);
static_assert(rstd::Impled<rstd::array<int, 3>, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::array<int, 3>, rstd::ops::DerefMut>);
static_assert(sizeof(rstd::array<rstd::u8, 4>) == sizeof(rstd::byte) * 4);
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::array<rstd::u8, 4>&>().data()),
                                 rstd::byte*>);
static_assert(
    rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::array<rstd::u8, 4> const&>().data()),
                       rstd::byte const*>);

static_assert([] {
    auto values      = rstd::array<rstd::u8, 3> { rstd::u8(1), rstd::u8(2), rstd::u8(3) };
    values[usize(1)] = rstd::u8(9);
    auto first       = values.first_mut().unwrap();
    auto last        = values.last_mut().unwrap();
    first            = rstd::u8(4);
    last             = rstd::u8(7);
    auto empty       = rstd::array<rstd::u8, 0> {};
    return values.first()->get() == rstd::u8(4) && values[usize(1)] == rstd::u8(9) &&
           values.last()->get() == rstd::u8(7) && values.data()[2] == rstd::byte { 7 } &&
           empty.first().is_none() && empty.last().is_none() && empty.first_mut().is_none() &&
           empty.last_mut().is_none();
}());

TEST(Array, OwnsAndBorrowsFixedStorage) {
    auto values = rstd::array<int, 3> { 2, 3, 5 };

    EXPECT_EQ(values.len(), usize(3));
    EXPECT_FALSE(values.is_empty());
    EXPECT_EQ(values[usize(1)], 3);

    auto borrowed = values.as_slice();
    EXPECT_EQ(borrowed.len(), usize(3));
    EXPECT_EQ(borrowed[usize(2)], 5);

    auto mutable_borrow      = values.as_mut_slice();
    mutable_borrow[usize(1)] = 7;
    EXPECT_EQ(values[usize(1)], 7);

    auto trait_borrow      = rstd::as_mut<int[]>(values);
    trait_borrow[usize(1)] = 9;
    EXPECT_EQ(values[usize(1)], 9);

    auto middle = values.get_mut(usize(1));
    ASSERT_TRUE(middle.is_some());
    **middle = 11;
    EXPECT_EQ(values[usize(1)], 11);
    EXPECT_TRUE(values.get(usize(3)).is_none());
    EXPECT_EQ(**values.first(), 2);
    EXPECT_EQ(**values.last(), 5);
    values.first_mut()->get_mut() = 13;
    values.last_mut()->get_mut()  = 17;
    EXPECT_EQ(**values.first(), 13);
    EXPECT_EQ(**values.last(), 17);
}

TEST(Array, EmptyArrayDoesNotConstructElementStorage) {
    auto values = rstd::array<NoDefault, 0> {};

    EXPECT_EQ(values.len(), usize());
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(values.data(), nullptr);
    EXPECT_EQ(values.as_slice().len(), usize());
    EXPECT_TRUE(values.first().is_none());
    EXPECT_TRUE(values.last().is_none());
    EXPECT_TRUE(values.first_mut().is_none());
    EXPECT_TRUE(values.last_mut().is_none());

    auto mapped = rstd::move(values).map([](NoDefault) {
        return 1;
    });
    EXPECT_TRUE(mapped.is_empty());
}

TEST(Array, IteratesByBorrowAndByValue) {
    auto values = rstd::array<int, 3> { 1, 2, 3 };
    auto iter   = values.iter();

    EXPECT_EQ(**iter.next(), 1);
    EXPECT_EQ(**iter.next_back(), 3);
    EXPECT_EQ(iter.len(), usize(1));

    auto owned = rstd::move(values).into_iter();
    static_assert(rstd::Impled<decltype(owned), rstd::iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<decltype(owned), rstd::iter::ExactSizeIterator>);
    EXPECT_EQ(owned.next(), Some(1));
    EXPECT_EQ(owned.next_back(), Some(3));
    EXPECT_EQ(owned.next(), Some(2));
    EXPECT_TRUE(owned.next().is_none());
}

TEST(Array, U8RangeForUsesLogicalElements) {
    auto values = rstd::array<rstd::u8, 3> { rstd::u8(2), rstd::u8(3), rstd::u8(5) };
    for (auto value : values) value = rstd::u8(value.get().to_primitive() + 1);

    const auto& immutable = values;
    auto        total     = rstd::u8();
    for (auto value : immutable) total += value;
    EXPECT_EQ(total, rstd::u8(13));
    EXPECT_EQ(values.data()[0], rstd::byte { 3 });
}

TEST(Array, ClonesElementsAndMapsOwnedValues) {
    auto source           = rstd::array<CloneOnly, 2> { CloneOnly { 4 }, CloneOnly { 9 } };
    auto cloned           = source.clone();
    cloned[usize()].value = 7;

    EXPECT_EQ(source[usize()].value, 4);
    EXPECT_EQ(cloned[usize()].value, 7);

    auto mapped = rstd::move(cloned).map([](CloneOnly value) {
        return value.value * 2;
    });
    EXPECT_EQ(mapped, (rstd::array<int, 2> { 14, 18 }));
}

TEST(Array, ProducesElementReferencesAndGeneratedValues) {
    auto values   = rstd::array<int, 3> { 3, 4, 5 };
    auto refs     = values.each_ref();
    auto refs_mut = values.each_mut();

    EXPECT_EQ(*refs[usize()], 3);
    *refs_mut[usize(2)] = 8;
    EXPECT_EQ(values[usize(2)], 8);

    auto generated = rstd::array_::from_fn<4>([](usize index) {
        return static_cast<int>((index * index).to_primitive());
    });
    EXPECT_EQ(generated, (rstd::array<int, 4> { 0, 1, 4, 9 }));
}

TEST(Array, SupportsStructuredBindings) {
    auto [first, second, third] = rstd::array<int, 3> { 8, 13, 21 };

    EXPECT_EQ(first, 8);
    EXPECT_EQ(second, 13);
    EXPECT_EQ(third, 21);
}

TEST(Array, U8UsesByteStorageWithLogicalIteration) {
    auto values = rstd::array<rstd::u8, 3> { rstd::u8(4), rstd::u8(5), rstd::u8(6) };
    auto iter   = values.iter_mut();

    **iter.next() = rstd::u8(9);
    EXPECT_EQ(values[usize()], rstd::u8(9));
    EXPECT_EQ(values.data()[usize(1).to_primitive()], rstd::byte { 5 });

    auto owned = rstd::move(values).into_iter();
    EXPECT_EQ(owned.next(), rstd::Some(rstd::u8(9)));
    EXPECT_EQ(owned.next_back(), rstd::Some(rstd::u8(6)));
}

namespace
{
struct alignas(64) ArraySlotValue {
    int* live;
    int  value;
    ArraySlotValue(int& count, int value): live(&count), value(value) { ++*live; }
    ArraySlotValue(const ArraySlotValue&) = delete;
    ~ArraySlotValue() { --*live; }
};
} // namespace

TEST(Array, UninitSlotsUseAlignedByteOffsetsAndBorrowedIteration) {
    using Slot  = MaybeUninit<ArraySlotValue>;
    using Slots = rstd::array<Slot, 3>;
    static_assert(sizeof(Slots) == 3 * sizeof(ArraySlotValue));
    static_assert(alignof(Slots) == alignof(ArraySlotValue));
    int   live = 0;
    Slots slots {};
    EXPECT_EQ(live, 0);
    auto base = slots.as_mut_ptr().as_byte_ptr();
    for (usize index; index < slots.len(); ++index) {
        EXPECT_EQ(slots.as_mut_ptr().add(index).as_byte_ptr(),
                  base + index.to_primitive() * sizeof(ArraySlotValue));
        rstd::construct_at(slots[index].as_mut_ptr(), live, int(index.to_primitive() + 10));
    }
    EXPECT_EQ(live, 3);
    EXPECT_EQ(slots.get<1>().assume_init_ref().value, 11);
    const auto& immutable = slots;
    EXPECT_EQ(immutable.get<2>().assume_init_ref().value, 12);
    EXPECT_EQ(immutable.each_ref()[usize(1)]->assume_init_ref().value, 11);
    auto halves = rstd::slice_::split_at_mut(slots.as_mut_slice(), usize(1));
    EXPECT_EQ(halves.get<1>()[usize()].assume_init_ref().value, 11);
    auto iter = slots.iter_mut();
    iter.next_back()->get_mut().assume_init_drop();
    EXPECT_EQ(live, 2);
    iter.next()->get_mut().assume_init_drop();
    iter.next()->get_mut().assume_init_drop();
    EXPECT_EQ(live, 0);
    EXPECT_TRUE(iter.next().is_none());
}

TEST(Array, EmptyUninitSlotsExposeEmptyBorrows) {
    rstd::array<MaybeUninit<ArraySlotValue>, 0> slots {};
    EXPECT_EQ(slots.data(), nullptr);
    EXPECT_EQ(slots.begin(), slots.end());
    EXPECT_TRUE(slots.as_slice().is_empty());
    EXPECT_TRUE(slots.get_mut(usize()).is_none());
    EXPECT_TRUE(slots.iter_mut().next().is_none());
}

TEST(Array, UninitSlotFactoriesAndCopiesPreserveTrivialPayloads) {
    using Slot  = MaybeUninit<int>;
    auto slots  = rstd::array<Slot, 3>::from_fn([](usize index) {
        return Slot::make(int(index.to_primitive() + 20));
    });
    auto copied = slots;
    auto moved  = rstd::move(copied);
    EXPECT_EQ(moved.get<0>().assume_init_read(), 20);
    EXPECT_EQ(moved.get<2>().assume_init_read(), 22);
    EXPECT_EQ(moved.get<1>().assume_init_read(), 21);
    for (usize index; index < slots.len(); ++index) {
        slots[index].assume_init_drop();
        copied[index].assume_init_drop();
        moved[index].assume_init_drop();
    }
}

#if __has_feature(memory_sanitizer)
extern "C" rstd::intptr_t __msan_test_shadow(const volatile void*, rstd::size_t);

TEST(Array, UninitSlotsKeepStackAndHeapStoragePoisoned) {
    using Slots = rstd::array<MaybeUninit<int>, 3>;
    auto check  = [](Slots& slots) {
        auto bytes = slots.as_ptr().as_byte_ptr();
        for (rstd::size_t i = 0; i < sizeof(Slots); ++i)
            EXPECT_EQ(__msan_test_shadow(bytes + i, 1), rstd::intptr_t(0));
        slots[usize(1)].write(42);
        EXPECT_EQ(__msan_test_shadow(slots.get<1>().as_ptr(), sizeof(int)), rstd::intptr_t(-1));
        EXPECT_EQ(__msan_test_shadow(slots.get<0>().as_ptr(), sizeof(int)), rstd::intptr_t(0));
        slots[usize(1)].assume_init_drop();
    };
    Slots plain;
    Slots braces {};
    check(plain);
    check(braces);
    auto heap = ::alloc::raw_new<Slots>();
    check(*heap);
    ::alloc::raw_delete(heap);
}
#endif

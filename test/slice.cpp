#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;

namespace
{

struct CloneOnly : rstd::DefaultInClass<CloneOnly, rstd::clone::Clone> {
    int  value;
    int* clone_froms;

    CloneOnly(int value, int& clone_froms): value(value), clone_froms(&clone_froms) {}
    CloneOnly(const CloneOnly&)                    = delete;
    auto operator=(const CloneOnly&) -> CloneOnly& = delete;
    CloneOnly(CloneOnly&&)                         = default;
    auto operator=(CloneOnly&&) -> CloneOnly&      = default;

    auto clone() const -> CloneOnly { return CloneOnly { value, *clone_froms }; }
    void clone_from(const CloneOnly& source) {
        value = source.value;
        ++*clone_froms;
    }
};

struct NonTrivialDrop {
    int value;
    ~NonTrivialDrop() {}
};

template<typename T>
concept SliceCopyable = requires(rstd::mut_ref<T[]> destination, rstd::slice<T> source) {
    rstd::slice_::copy_from_slice(destination, source);
};

template<typename Pointer>
concept U8SliceRawParts =
    requires(Pointer pointer) { rstd::slice<rstd::u8>::from_raw_parts(pointer, usize()); };

template<typename Pointer>
concept U8MutSliceRawParts =
    requires(Pointer pointer) { rstd::mut_ref<rstd::u8[]>::from_raw_parts(pointer, usize()); };

static_assert(! rstd::mtp::triv_copyable<NonTrivialDrop>);
static_assert(! rstd::Impled<NonTrivialDrop, rstd::Copy>);
static_assert(SliceCopyable<int>);
static_assert(! SliceCopyable<CloneOnly>);
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::slice<rstd::u8>>().as_raw_ptr()),
                                 rstd::byte const*>);
static_assert(
    rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::mut_ref<rstd::u8[]>>().as_raw_ptr()),
                       rstd::byte*>);
static_assert(U8SliceRawParts<rstd::byte const*>);
static_assert(U8MutSliceRawParts<rstd::byte*>);
static_assert(! U8SliceRawParts<rstd::u8*>);
static_assert(! U8SliceRawParts<rstd::uint8_t*>);
static_assert(! U8MutSliceRawParts<rstd::u8*>);
static_assert(! U8MutSliceRawParts<rstd::uint8_t*>);
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::slice<rstd::u8>>().begin()),
                                 rstd::ptr<rstd::u8>>);
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::mut_ref<rstd::u8[]>&>().begin()),
                                 rstd::mut_ptr<rstd::u8>>);

} // namespace

TEST(Slice, SplitAtMutProducesDisjointViews) {
    int  values[] { 1, 2, 3, 4 };
    auto whole = rstd::mut_ref<int[]>::from_raw_parts(values, usize(4));

    auto [left, right] = rstd::slice_::split_at_mut(whole, usize(2));
    ASSERT_EQ(left.len(), usize(2));
    ASSERT_EQ(right.len(), usize(2));

    left[usize(1)] = 7;
    right[usize()] = 9;
    EXPECT_EQ(values[1], 7);
    EXPECT_EQ(values[2], 9);

    auto [empty_left, all_right] = rstd::slice_::split_at_mut(whole, usize());
    EXPECT_TRUE(empty_left.is_empty());
    EXPECT_EQ(all_right.len(), usize(4));

    auto [all_left, empty_right] = rstd::slice_::split_at_mut(whole, usize(4));
    EXPECT_EQ(all_left.len(), usize(4));
    EXPECT_TRUE(empty_right.is_empty());
}

TEST(Slice, SplitAtMutRejectsOutOfBoundsIndex) {
    int  values[] { 1, 2 };
    auto whole = rstd::mut_ref<int[]>::from_raw_parts(values, usize(2));
    EXPECT_DEATH((void)rstd::slice_::split_at_mut(whole, usize(3)),
                 "slice split index out of bounds");
}

TEST(Slice, CopyFromSliceCopiesTypedElements) {
    rstd::u64 source[] { rstd::u64(11), rstd::u64(22), rstd::u64(33) };
    rstd::u64 destination[] { rstd::u64(), rstd::u64(), rstd::u64() };

    rstd::slice_::copy_from_slice(rstd::mut_ref<rstd::u64[]>::from_raw_parts(destination, usize(3)),
                                  rstd::slice<rstd::u64>::from_raw_parts(source, usize(3)));

    EXPECT_EQ(destination[0], rstd::u64(11));
    EXPECT_EQ(destination[1], rstd::u64(22));
    EXPECT_EQ(destination[2], rstd::u64(33));
}

TEST(Slice, CopyFromSliceAcceptsEmptySlices) {
    rstd::slice_::copy_from_slice(rstd::mut_ref<int[]>(), rstd::slice<int>());
}

TEST(Slice, U8ViewReadsAndWritesByteStorageByValue) {
    rstd::byte storage[] { rstd::byte { 1 }, rstd::byte { 2 }, rstd::byte { 255 } };
    auto       values = rstd::mut_ref<rstd::u8[]>::from_raw_parts(storage, usize(3));

    EXPECT_EQ(values[usize()], rstd::u8(1));
    values[usize(1)] = rstd::u8(128);
    EXPECT_EQ(storage[1], rstd::byte { 128 });

    auto immutable = values.as_ref();
    static_assert(rstd::mtp::same_as<decltype(immutable[usize()]), rstd::u8>);
    EXPECT_EQ(immutable[usize(2)], rstd::u8(255));
}

TEST(Slice, RangeForUsesLogicalU8ValuesAndProxies) {
    rstd::byte storage[] { rstd::byte { 1 }, rstd::byte { 2 }, rstd::byte { 3 } };
    auto       values = rstd::mut_ref<rstd::u8[]>::from_raw_parts(storage, usize(3));

    for (auto value : values) value = rstd::u8(value.get().to_primitive() + 2);
    EXPECT_EQ(storage[0], rstd::byte { 3 });
    EXPECT_EQ(storage[2], rstd::byte { 5 });

    auto immutable = values.as_ref();
    auto total     = rstd::u8();
    for (auto value : immutable) total += value;
    EXPECT_EQ(total, rstd::u8(12));
}

TEST(Slice, CloneFromSliceReusesInitializedElements) {
    int       clone_froms = 0;
    CloneOnly source[] { CloneOnly { 4, clone_froms }, CloneOnly { 9, clone_froms } };
    CloneOnly destination[] { CloneOnly { 1, clone_froms }, CloneOnly { 2, clone_froms } };

    rstd::slice_::clone_from_slice(
        rstd::mut_ref<CloneOnly[]>::from_raw_parts(destination, usize(2)),
        rstd::slice<CloneOnly>::from_raw_parts(source, usize(2)));

    EXPECT_EQ(destination[0].value, 4);
    EXPECT_EQ(destination[1].value, 9);
    EXPECT_EQ(clone_froms, 2);
}

TEST(Slice, FromSliceRejectsLengthMismatch) {
    int source[] { 1, 2 };
    int destination[] { 0 };

    auto source_slice      = rstd::slice<int>::from_raw_parts(source, usize(2));
    auto destination_slice = rstd::mut_ref<int[]>::from_raw_parts(destination, usize(1));

    EXPECT_DEATH(rstd::slice_::copy_from_slice(destination_slice, source_slice),
                 "source and destination slices have different lengths");
}

TEST(Slice, CloneFromSliceRejectsLengthMismatch) {
    int       clone_froms = 0;
    CloneOnly source[] { CloneOnly { 1, clone_froms }, CloneOnly { 2, clone_froms } };
    CloneOnly destination[] { CloneOnly { 0, clone_froms } };

    auto source_slice      = rstd::slice<CloneOnly>::from_raw_parts(source, usize(2));
    auto destination_slice = rstd::mut_ref<CloneOnly[]>::from_raw_parts(destination, usize(1));

    EXPECT_DEATH(rstd::slice_::clone_from_slice(destination_slice, source_slice),
                 "source and destination slices have different lengths");
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

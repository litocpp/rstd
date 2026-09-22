#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

template<typename T>
concept MutableEndpoints = requires(T& value) {
    value.first_mut();
    value.last_mut();
};

static_assert(! MutableEndpoints<slice<int>>);
static_assert(MutableEndpoints<mut_ref<int[]>>);
static_assert(! MutableEndpoints<ref<int>>);
static_assert(
    rstd::mtp::same_as<decltype(rstd::mtp::declval<mut_ref<u8[]>>().last()), Option<ref<u8>>>);
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<mut_ref<u8[]>>().last_mut()),
                                 Option<mut_ref<u8>>>);

constexpr bool slice_endpoints() {
    slice<int>     empty;
    mut_ref<int[]> mutable_empty;
    if (empty.first().is_some() || empty.last().is_some() || mutable_empty.first_mut().is_some() ||
        mutable_empty.last_mut().is_some())
        return false;
    int  data[] = { 1, 2, 3 };
    auto values = mut_ref<int[]>::from_raw_parts(data, 3_usize);
    if (values.first()->as_raw_ptr() != data || values.last()->as_raw_ptr() != data + 2)
        return false;
    values.first_mut()->get_mut() = 4;
    values.last_mut()->get_mut()  = 7;
    auto readonly                 = values.as_ref();
    if (readonly.first()->get() != 4 || readonly.last()->get() != 7) return false;
    auto single = slice<int>::from_raw_parts(data, 1_usize);
    return single.first()->as_raw_ptr() == single.last()->as_raw_ptr();
}

constexpr bool byte_endpoints() {
    mut_ref<u8[]> empty;
    if (empty.first().is_some() || empty.last().is_some() || empty.first_mut().is_some() ||
        empty.last_mut().is_some())
        return false;
    rstd::byte data[] = { rstd::byte { 1 }, rstd::byte { 2 } };
    auto       values = mut_ref<u8[]>::from_raw_parts(data, 2_usize);
    if (values.first()->as_raw_ptr() != data || values.last()->as_raw_ptr() != data + 1)
        return false;
    auto first = values.first_mut().unwrap();
    auto last  = values.last_mut().unwrap();
    first      = u8(4);
    last       = u8(9);
    return values.first()->get() == u8(4) && values.last()->get() == u8(9) &&
           data[0] == rstd::byte { 4 } && data[1] == rstd::byte { 9 };
}

static_assert(slice_endpoints());
static_assert(byte_endpoints());

TEST(Slice, EndpointsBorrowObjectsAndBytes) {
    EXPECT_TRUE(slice_endpoints());
    EXPECT_TRUE(byte_endpoints());
}

TEST(Slice, EndpointOutlivesTemporaryView) {
    int  data[] = { 1, 2 };
    auto last   = [](int* data) {
        auto view = slice<int>::from_raw_parts(data, 2_usize);
        return view.last().unwrap();
    }(data);
    data[1] = 7;
    EXPECT_EQ(last.get(), 7);
}

TEST(Slice, SwapBoundsAndMoveOnly) {
    struct Value {
        int key;
        explicit Value(int key): key(key) {}
        Value(Value&&)                    = default;
        Value(const Value&)               = delete;
        auto operator=(Value&&) -> Value& = delete;
    };
    Value rows[] = { Value(1), Value(2) };
    auto  values = mut_ref<Value[]>::from_raw_parts(rows, 2_usize);
    rstd::slice_::swap(values, 0_usize, 1_usize);
    EXPECT_EQ(rows[0].key, 2);
    EXPECT_EQ(rows[1].key, 1);
    rstd::slice_::swap(values, 0_usize, 0_usize);
    EXPECT_EQ(rows[0].key, 2);
    EXPECT_DEATH(rstd::slice_::swap(values, 2_usize, 2_usize), "index out of bounds");
    EXPECT_DEATH(rstd::slice_::swap(mut_ref<u8[]> {}, 0_usize, 0_usize), "index out of bounds");
}

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

#include <gtest/gtest.h>

import rstd;

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
    void clone_from(CloneOnly& source) { *this = source.clone(); }
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

TEST(Array, OwnsAndBorrowsFixedStorage) {
    auto values = rstd::array<int, 3> { 2, 3, 5 };

    EXPECT_EQ(values.len(), 3u);
    EXPECT_FALSE(values.is_empty());
    EXPECT_EQ(values[1], 3);

    auto borrowed = values.as_slice();
    EXPECT_EQ(borrowed.len(), 3u);
    EXPECT_EQ(borrowed[2], 5);

    auto mutable_borrow = values.as_mut_slice();
    mutable_borrow[1]   = 7;
    EXPECT_EQ(values[1], 7);

    auto trait_borrow = rstd::as_mut<int[]>(values);
    trait_borrow[1]   = 9;
    EXPECT_EQ(values[1], 9);

    auto middle = values.get_mut(1);
    ASSERT_TRUE(middle.is_some());
    **middle = 11;
    EXPECT_EQ(values[1], 11);
    EXPECT_TRUE(values.get(3).is_none());
    EXPECT_EQ(**values.first(), 2);
    EXPECT_EQ(**values.last(), 5);
}

TEST(Array, EmptyArrayDoesNotConstructElementStorage) {
    auto values = rstd::array<NoDefault, 0> {};

    EXPECT_EQ(values.len(), 0u);
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(values.data(), nullptr);
    EXPECT_EQ(values.as_slice().len(), 0u);
    EXPECT_TRUE(values.first().is_none());
    EXPECT_TRUE(values.last().is_none());

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
    EXPECT_EQ(iter.len(), 1u);

    auto owned = rstd::move(values).into_iter();
    static_assert(rstd::Impled<decltype(owned), rstd::iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<decltype(owned), rstd::iter::ExactSizeIterator>);
    EXPECT_EQ(owned.next(), Some(1));
    EXPECT_EQ(owned.next_back(), Some(3));
    EXPECT_EQ(owned.next(), Some(2));
    EXPECT_TRUE(owned.next().is_none());
}

TEST(Array, ClonesElementsAndMapsOwnedValues) {
    auto source     = rstd::array<CloneOnly, 2> { CloneOnly { 4 }, CloneOnly { 9 } };
    auto cloned     = source.clone();
    cloned[0].value = 7;

    EXPECT_EQ(source[0].value, 4);
    EXPECT_EQ(cloned[0].value, 7);

    auto mapped = rstd::move(cloned).map([](CloneOnly value) {
        return value.value * 2;
    });
    EXPECT_EQ(mapped, (rstd::array<int, 2> { 14, 18 }));
}

TEST(Array, ProducesElementReferencesAndGeneratedValues) {
    auto values   = rstd::array<int, 3> { 3, 4, 5 };
    auto refs     = values.each_ref();
    auto refs_mut = values.each_mut();

    EXPECT_EQ(*refs[0], 3);
    *refs_mut[2] = 8;
    EXPECT_EQ(values[2], 8);

    auto generated = rstd::array_::from_fn<4>([](usize index) {
        return static_cast<int>(index * index);
    });
    EXPECT_EQ(generated, (rstd::array<int, 4> { 0, 1, 4, 9 }));
}

TEST(Array, SupportsStructuredBindings) {
    auto [first, second, third] = rstd::array<int, 3> { 8, 13, 21 };

    EXPECT_EQ(first, 8);
    EXPECT_EQ(second, 13);
    EXPECT_EQ(third, 21);
}

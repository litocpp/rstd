#include <gtest/gtest.h>
import rstd;

static_assert(rstd::Impled<rstd::vec::Vec<int>, rstd::ops::Deref>);
static_assert(rstd::Impled<rstd::vec::Vec<int>, rstd::ops::DerefMut>);
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::vec::Vec<rstd::u8>&>().data()),
                                 rstd::byte*>);
static_assert(
    rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::vec::Vec<rstd::u8> const&>().data()),
                       rstd::byte const*>);
static_assert(rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::vec::Vec<rstd::u8>&>().begin()),
                                 rstd::mut_ptr<rstd::u8>>);
static_assert(
    rstd::mtp::same_as<decltype(rstd::mtp::declval<rstd::vec::Vec<rstd::u8> const&>().begin()),
                       rstd::ptr<rstd::u8>>);
static_assert(! rstd::mtp::convertible_to<rstd::vec::SpareSlot<rstd::u8>, rstd::u8>);

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::string::String;
using rstd::vec::Vec;

namespace
{

template<typename T>
concept ConcreteCloneable = requires(const T& value) { value.clone(); };

struct MoveOnly {
    MoveOnly()                           = default;
    MoveOnly(const MoveOnly&)            = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&)                 = default;
    MoveOnly& operator=(MoveOnly&&)      = default;
};

struct SelfPointing {
    int        value;
    const int* value_ptr;

    explicit SelfPointing(int value): value(value), value_ptr(&this->value) {}
    SelfPointing(const SelfPointing&)            = delete;
    SelfPointing& operator=(const SelfPointing&) = delete;
    SelfPointing(SelfPointing&& other) noexcept: value(other.value), value_ptr(&value) {}
    SelfPointing& operator=(SelfPointing&& other) noexcept {
        value     = other.value;
        value_ptr = &value;
        return *this;
    }
};

struct HintedCollectIterator : rstd::DefaultInClass<HintedCollectIterator, rstd::iter::Iterator> {
    using Item = int;

    int  current;
    int  end;
    int* hint_calls;
    bool report_remaining;

    HintedCollectIterator(int end, int& hint_calls, bool report_remaining)
        : current(0), end(end), hint_calls(&hint_calls), report_remaining(report_remaining) {}

    auto next() -> rstd::Option<int> {
        if (current == end) return rstd::None();
        return rstd::Some(current++);
    }

    auto size_hint() const -> rstd::iter::SizeHint {
        ++*hint_calls;
        auto remaining = usize(static_cast<rstd::size_t>(end - current));
        return { report_remaining ? remaining : usize(), rstd::None() };
    }
};

struct CollectMoveOnly {
    int        value;
    const int* value_ptr;
    int*       drops;

    CollectMoveOnly(int value, int& drops): value(value), value_ptr(&this->value), drops(&drops) {}
    CollectMoveOnly(const CollectMoveOnly&)                    = delete;
    auto operator=(const CollectMoveOnly&) -> CollectMoveOnly& = delete;
    CollectMoveOnly(CollectMoveOnly&& other) noexcept
        : value(other.value), value_ptr(&value), drops(other.drops) {
        other.drops = nullptr;
    }
    auto operator=(CollectMoveOnly&&) -> CollectMoveOnly& = delete;
    ~CollectMoveOnly() {
        if (drops != nullptr) ++*drops;
    }
};

struct CloneTracked : rstd::DefaultInClass<CloneTracked, rstd::clone::Clone> {
    int  value;
    int* clones;
    int* clone_froms;

    CloneTracked(int value, int& clones, int& clone_froms)
        : value(value), clones(&clones), clone_froms(&clone_froms) {}
    CloneTracked(const CloneTracked&)                    = delete;
    auto operator=(const CloneTracked&) -> CloneTracked& = delete;
    CloneTracked(CloneTracked&&)                         = default;
    auto operator=(CloneTracked&&) -> CloneTracked&      = default;

    auto clone() const -> CloneTracked {
        ++*clones;
        return CloneTracked { value, *clones, *clone_froms };
    }

    void clone_from(const CloneTracked& source) {
        value = source.value;
        ++*clone_froms;
    }
};

struct InPlaceWide {
    int first;
    int second;
    int third;
};

struct InPlaceNarrow {
    int first;
    int second;
};

struct alignas(8) InPlaceAlignedLong {
    rstd::int64_t value;
};

struct InPlaceSourceTracked {
    int        value;
    const int* value_ptr;
    int*       drops;

    InPlaceSourceTracked(int value, int& drops)
        : value(value), value_ptr(&this->value), drops(&drops) {}
    InPlaceSourceTracked(const InPlaceSourceTracked&)                    = delete;
    auto operator=(const InPlaceSourceTracked&) -> InPlaceSourceTracked& = delete;
    InPlaceSourceTracked(InPlaceSourceTracked&& other) noexcept
        : value(other.value), value_ptr(&value), drops(other.drops) {
        other.drops = nullptr;
    }
    auto operator=(InPlaceSourceTracked&&) -> InPlaceSourceTracked& = delete;
    ~InPlaceSourceTracked() {
        if (drops != nullptr) ++*drops;
    }
};

struct InPlaceDestinationTracked {
    int        value;
    const int* value_ptr;
    int*       drops;

    InPlaceDestinationTracked(int value, int& drops)
        : value(value), value_ptr(&this->value), drops(&drops) {}
    InPlaceDestinationTracked(const InPlaceDestinationTracked&)                    = delete;
    auto operator=(const InPlaceDestinationTracked&) -> InPlaceDestinationTracked& = delete;
    InPlaceDestinationTracked(InPlaceDestinationTracked&& other) noexcept
        : value(other.value), value_ptr(&value), drops(other.drops) {
        other.drops = nullptr;
    }
    auto operator=(InPlaceDestinationTracked&&) -> InPlaceDestinationTracked& = delete;
    ~InPlaceDestinationTracked() {
        if (drops != nullptr) ++*drops;
    }
};

static_assert(sizeof(InPlaceWide) == 12);
static_assert(sizeof(InPlaceNarrow) == 8);
static_assert(alignof(InPlaceWide) == alignof(InPlaceNarrow));
static_assert(sizeof(InPlaceSourceTracked) == sizeof(InPlaceDestinationTracked));
static_assert(alignof(InPlaceSourceTracked) == alignof(InPlaceDestinationTracked));

template<typename T>
concept ConstructibleFromSlice = requires(rstd::slice<T> values) { Vec<T>::from(values); };

static_assert(ConcreteCloneable<Vec<String>>);
static_assert(! ConcreteCloneable<Vec<MoveOnly>>);
static_assert(ConstructibleFromSlice<int>);
static_assert(ConstructibleFromSlice<CloneTracked>);
static_assert(! ConstructibleFromSlice<MoveOnly>);
static_assert(rstd::Impled<Vec<int>, rstd::convert::From<rstd::slice<int>>>);
static_assert(rstd::Impled<Vec<CloneTracked>, rstd::convert::From<rstd::slice<CloneTracked>>>);
static_assert(! rstd::Impled<Vec<MoveOnly>, rstd::convert::From<rstd::slice<MoveOnly>>>);

} // namespace

TEST(Vec, BasicPushPop) {
    Vec<int> v;
    EXPECT_EQ(v.len(), usize());
    EXPECT_TRUE(v.is_empty());

    v.push(1);
    v.push(2);
    v.push(3);

    EXPECT_EQ(v.len(), usize(3));
    EXPECT_FALSE(v.is_empty());

    EXPECT_EQ(v.pop(), Some(3));
    EXPECT_EQ(v.pop(), Some(2));
    EXPECT_EQ(v.pop(), Some(1));
    EXPECT_TRUE(v.pop().is_none());
    EXPECT_EQ(v.len(), usize());
}

TEST(Vec, RangeForUsesLogicalElements) {
    Vec<int> integers;
    integers.push(1);
    integers.push(2);
    for (auto& value : integers) value *= 3;
    EXPECT_EQ(integers[usize()], 3);
    EXPECT_EQ(integers[usize(1)], 6);

    Vec<u8> bytes;
    bytes.push(u8(4));
    bytes.push(u8(5));
    for (auto value : bytes) value = u8(value.get().to_primitive() + 1);
    EXPECT_EQ(bytes[usize()], u8(5));
    EXPECT_EQ(bytes[usize(1)], u8(6));

    auto const& immutable = bytes;
    auto        total     = u8();
    for (auto value : immutable) total += value;
    EXPECT_EQ(total, u8(11));
}

TEST(Vec, Growth) {
    Vec<int> v = Vec<int>::with_capacity(usize(2));
    EXPECT_EQ(v.capacity(), usize(2));

    v.push(1);
    v.push(2);
    EXPECT_EQ(v.capacity(), usize(2));

    v.push(3);
    EXPECT_GT(v.capacity(), usize(2));
    EXPECT_EQ(v.len(), usize(3));

    EXPECT_EQ(v[usize()], 1);
    EXPECT_EQ(v[usize(1)], 2);
    EXPECT_EQ(v[usize(2)], 3);
}

TEST(Vec, GrowthMovesNonTrivialElements) {
    Vec<SelfPointing> values;
    for (int value = 0; value < 9; ++value) values.emplace_back(value);

    ASSERT_EQ(values.len(), usize(9));
    for (usize index {}; index < values.len(); ++index) {
        EXPECT_EQ(values[index].value, static_cast<int>(index.to_primitive()));
        EXPECT_EQ(values[index].value_ptr, &values[index].value);
    }
}

TEST(Vec, CollectUsesTrustedLengthAsExactCapacity) {
    auto values = rstd::iter::range(0_i32, 10_i32).collect<Vec<i32>>();

    EXPECT_EQ(values.len(), usize(10));
    EXPECT_EQ(values.capacity(), usize(10));
}

TEST(Vec, CollectUsesGeneralIteratorLowerBoundAfterFirstItem) {
    int  hint_calls = 0;
    auto values     = HintedCollectIterator(10, hint_calls, true).collect<Vec<int>>();

    EXPECT_EQ(values.len(), usize(10));
    EXPECT_EQ(values.capacity(), usize(10));
    EXPECT_GE(hint_calls, 1);

    hint_calls = 0;
    auto empty = HintedCollectIterator(0, hint_calls, true).collect<Vec<int>>();
    EXPECT_TRUE(empty.is_empty());
    EXPECT_EQ(empty.capacity(), usize());
    EXPECT_EQ(hint_calls, 0);
}

TEST(Vec, CollectGrowsWhenGeneralIteratorUnderestimates) {
    int  hint_calls = 0;
    auto values     = HintedCollectIterator(10, hint_calls, false).collect<Vec<int>>();

    ASSERT_EQ(values.len(), usize(10));
    EXPECT_EQ(values.capacity(), usize(16));
    for (int index = 0; index < 10; ++index) {
        EXPECT_EQ(values[usize(static_cast<rstd::size_t>(index))], index);
    }
}

TEST(Vec, CollectUsesElementSpecificMinimumCapacity) {
    bool emitted = false;
    auto values  = rstd::iter::from_fn([&emitted]() -> rstd::Option<u8> {
                      if (emitted) return rstd::None();
                      emitted = true;
                      return rstd::Some(u8(7));
                   }).collect<Vec<u8>>();

    EXPECT_EQ(values.len(), usize(1));
    EXPECT_EQ(values.capacity(), usize(8));
    EXPECT_EQ(values[usize()], u8(7));
}

TEST(Vec, CollectReusesUnadvancedIntoIterAllocation) {
    auto source = Vec<int>::with_capacity(usize(12));
    source.push(3);
    source.push(5);
    source.push(8);
    auto* allocation = source.data();

    auto values = source.into_iter().collect<Vec<int>>();

    EXPECT_EQ(values.data(), allocation);
    EXPECT_EQ(values.capacity(), usize(12));
    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()], 3);
    EXPECT_EQ(values[usize(1)], 5);
    EXPECT_EQ(values[usize(2)], 8);
}

TEST(Vec, CollectCompactsAdvancedIntoIterInPlace) {
    int drops = 0;
    {
        auto source = Vec<CollectMoveOnly>::with_capacity(usize(8));
        for (int value = 0; value < 8; ++value) source.emplace_back(value, drops);
        auto* allocation = source.data();
        auto  iterator   = source.into_iter();
        {
            auto first  = iterator.next();
            auto second = iterator.next();
            ASSERT_TRUE(first.is_some());
            ASSERT_TRUE(second.is_some());
            EXPECT_EQ(first->value, 0);
            EXPECT_EQ(second->value, 1);
        }
        EXPECT_EQ(drops, 2);

        auto values = rstd::move(iterator).collect<Vec<CollectMoveOnly>>();

        EXPECT_EQ(values.data(), allocation);
        EXPECT_EQ(values.capacity(), usize(8));
        ASSERT_EQ(values.len(), usize(6));
        for (usize index {}; index < values.len(); ++index) {
            auto expected = static_cast<int>(index.to_primitive()) + 2;
            EXPECT_EQ(values[index].value, expected);
            EXPECT_EQ(values[index].value_ptr, &values[index].value);
        }
    }
    EXPECT_EQ(drops, 8);
}

TEST(Vec, CollectCompactsWhenConsumedPrefixExceedsRemainder) {
    int drops = 0;
    {
        auto source = Vec<CollectMoveOnly>::with_capacity(usize(3));
        for (int value = 0; value < 3; ++value) source.emplace_back(value, drops);
        auto* allocation = source.data();
        auto  iterator   = source.into_iter();
        {
            auto first  = iterator.next();
            auto second = iterator.next();
            ASSERT_TRUE(first.is_some());
            ASSERT_TRUE(second.is_some());
        }

        auto values = rstd::move(iterator).collect<Vec<CollectMoveOnly>>();

        EXPECT_EQ(values.data(), allocation);
        EXPECT_EQ(values.capacity(), usize(3));
        ASSERT_EQ(values.len(), usize(1));
        EXPECT_EQ(values[usize()].value, 2);
        EXPECT_EQ(values[usize()].value_ptr, &values[usize()].value);
    }
    EXPECT_EQ(drops, 3);
}

TEST(Vec, CollectReallocatesSparseAdvancedIntoIter) {
    auto source = Vec<int>::with_capacity(usize(8));
    for (int value = 0; value < 8; ++value) source.push(rstd::move(value));
    auto* allocation = source.data();
    auto  iterator   = source.into_iter();
    for (int count = 0; count < 5; ++count) ASSERT_TRUE(iterator.next().is_some());

    auto values = rstd::move(iterator).collect<Vec<int>>();

    EXPECT_NE(values.data(), allocation);
    EXPECT_EQ(values.capacity(), usize(4));
    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()], 5);
    EXPECT_EQ(values[usize(1)], 6);
    EXPECT_EQ(values[usize(2)], 7);
}

TEST(Vec, IntoIterEndsYieldedSlotsAndTransfersRemainingOwnership) {
    int drops = 0;
    {
        auto source = Vec<CollectMoveOnly>::with_capacity(usize(4));
        for (int value = 0; value < 4; ++value) source.emplace_back(value, drops);
        auto iterator = source.into_iter();
        {
            auto front = iterator.next();
            auto back  = iterator.next_back();
            ASSERT_TRUE(front.is_some());
            ASSERT_TRUE(back.is_some());
            EXPECT_EQ(front->value, 0);
            EXPECT_EQ(back->value, 3);
            EXPECT_EQ(front->value_ptr, &front->value);
            EXPECT_EQ(back->value_ptr, &back->value);
        }
        EXPECT_EQ(drops, 2);

        auto moved = rstd::move(iterator);
        EXPECT_EQ(moved.len(), usize(2));
        EXPECT_EQ(iterator.len(), usize());
    }
    EXPECT_EQ(drops, 4);
}

TEST(Vec, InPlaceCollectMapReusesAllocation) {
    auto source = Vec<int>::with_capacity(usize(12));
    source.push(3);
    source.push(5);
    source.push(8);
    auto* allocation = source.data();

    auto values = source.into_iter()
                      .map([](int value) {
                          return value * 2;
                      })
                      .collect<Vec<int>>();

    EXPECT_EQ(values.data(), allocation);
    EXPECT_EQ(values.capacity(), usize(12));
    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()], 6);
    EXPECT_EQ(values[usize(1)], 10);
    EXPECT_EQ(values[usize(2)], 16);
}

TEST(Vec, InPlaceCollectReusesHalfConsumedDoubleEndedSource) {
    auto source = Vec<int>::with_capacity(usize(8));
    for (int value = 0; value < 6; ++value) source.push(rstd::move(value));
    auto* allocation = source.data();
    auto  iterator   = source.into_iter();
    EXPECT_EQ(iterator.next(), rstd::Some(0));
    EXPECT_EQ(iterator.next_back(), rstd::Some(5));

    auto values = rstd::move(iterator)
                      .map([](int value) {
                          return value + 10;
                      })
                      .collect<Vec<int>>();

    EXPECT_EQ(values.data(), allocation);
    ASSERT_EQ(values.len(), usize(4));
    EXPECT_EQ(values[usize()], 11);
    EXPECT_EQ(values[usize(1)], 12);
    EXPECT_EQ(values[usize(2)], 13);
    EXPECT_EQ(values[usize(3)], 14);
}

TEST(Vec, InPlaceCollectAdapterPipelineCompactsInAllocation) {
    auto source = Vec<int>::with_capacity(usize(10));
    for (int value = 0; value < 8; ++value) source.push(rstd::move(value));
    auto* allocation = source.data();
    int   inspected  = 0;

    auto values = source.into_iter()
                      .map([](int value) {
                          return value + 1;
                      })
                      .filter([](int value) {
                          return value % 2 == 0;
                      })
                      .inspect([&inspected](int) {
                          ++inspected;
                      })
                      .collect<Vec<int>>();

    EXPECT_EQ(values.data(), allocation);
    EXPECT_EQ(values.capacity(), usize(10));
    EXPECT_EQ(inspected, 4);
    ASSERT_EQ(values.len(), usize(4));
    EXPECT_EQ(values[usize()], 2);
    EXPECT_EQ(values[usize(1)], 4);
    EXPECT_EQ(values[usize(2)], 6);
    EXPECT_EQ(values[usize(3)], 8);
}

TEST(Vec, InPlaceCollectEmptyResultKeepsCompatibleAllocation) {
    auto source = Vec<int>::with_capacity(usize(9));
    source.push(1);
    source.push(2);
    source.push(3);
    auto* allocation = source.data();

    auto values = source.into_iter()
                      .filter_map([](int) -> rstd::Option<int> {
                          return rstd::None();
                      })
                      .collect<Vec<int>>();

    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(values.data(), allocation);
    EXPECT_EQ(values.capacity(), usize(9));
}

TEST(Vec, InPlaceCollectPreservesPhysicalLayoutWithTrailingBytes) {
    auto source = Vec<InPlaceWide>::with_capacity(usize(3));
    source.push(InPlaceWide { 1, 2, 3 });
    source.push(InPlaceWide { 4, 5, 6 });
    source.push(InPlaceWide { 7, 8, 9 });
    auto* allocation = source.data();

    auto values = source.into_iter()
                      .map([](InPlaceWide value) {
                          return InPlaceNarrow { value.first, value.third };
                      })
                      .collect<Vec<InPlaceNarrow>>();

    EXPECT_EQ(static_cast<void*>(values.data()), static_cast<void*>(allocation));
    EXPECT_EQ(values.capacity(), usize(4));
    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()].first, 1);
    EXPECT_EQ(values[usize()].second, 3);
    EXPECT_EQ(values[usize(2)].first, 7);
    EXPECT_EQ(values[usize(2)].second, 9);

    auto boxed = values.into_boxed_slice();
    EXPECT_EQ(boxed.as_ptr().len(), usize(3));
    EXPECT_EQ(boxed.as_ptr()[usize(1)].first, 4);
    EXPECT_EQ(boxed.as_ptr()[usize(1)].second, 6);
}

TEST(Vec, InPlaceCollectedRemainderLayoutSupportsGrowth) {
    auto source = Vec<InPlaceWide>::with_capacity(usize(3));
    source.push(InPlaceWide { 1, 2, 3 });
    source.push(InPlaceWide { 4, 5, 6 });
    source.push(InPlaceWide { 7, 8, 9 });

    auto values = source.into_iter()
                      .map([](InPlaceWide value) {
                          return InPlaceNarrow { value.first, value.third };
                      })
                      .collect<Vec<InPlaceNarrow>>();

    ASSERT_EQ(values.capacity(), usize(4));
    values.push(InPlaceNarrow { 10, 11 });
    values.push(InPlaceNarrow { 12, 13 });

    ASSERT_EQ(values.len(), usize(5));
    EXPECT_GE(values.capacity(), usize(5));
    EXPECT_EQ(values[usize()].first, 1);
    EXPECT_EQ(values[usize(2)].second, 9);
    EXPECT_EQ(values[usize(3)].first, 10);
    EXPECT_EQ(values[usize(4)].second, 13);
}

TEST(Vec, InPlaceCollectMovesNonTrivialObjectsWithoutByteRelocation) {
    int source_drops      = 0;
    int destination_drops = 0;
    {
        auto source = Vec<InPlaceSourceTracked>::with_capacity(usize(6));
        for (int value = 0; value < 6; ++value) source.emplace_back(value, source_drops);
        auto* allocation = source.data();

        auto values =
            source.into_iter()
                .map([&destination_drops](InPlaceSourceTracked value) {
                    EXPECT_EQ(value.value_ptr, &value.value);
                    return InPlaceDestinationTracked { value.value * 3, destination_drops };
                })
                .collect<Vec<InPlaceDestinationTracked>>();

        EXPECT_EQ(static_cast<void*>(values.data()), static_cast<void*>(allocation));
        EXPECT_EQ(source_drops, 6);
        EXPECT_EQ(destination_drops, 0);
        ASSERT_EQ(values.len(), usize(6));
        for (usize index {}; index < values.len(); ++index) {
            EXPECT_EQ(values[index].value, static_cast<int>(index.to_primitive()) * 3);
            EXPECT_EQ(values[index].value_ptr, &values[index].value);
        }
    }
    EXPECT_EQ(source_drops, 6);
    EXPECT_EQ(destination_drops, 6);
}

TEST(Vec, InPlaceCollectTakeDropsSourceTail) {
    int drops = 0;
    {
        auto source = Vec<CollectMoveOnly>::with_capacity(usize(6));
        for (int value = 0; value < 6; ++value) source.emplace_back(value, drops);
        auto* allocation = source.data();

        auto values = source.into_iter().take(usize(2)).collect<Vec<CollectMoveOnly>>();

        EXPECT_EQ(values.data(), allocation);
        EXPECT_EQ(drops, 4);
        ASSERT_EQ(values.len(), usize(2));
        EXPECT_EQ(values[usize()].value, 0);
        EXPECT_EQ(values[usize(1)].value, 1);
    }
    EXPECT_EQ(drops, 6);
}

TEST(Vec, InPlaceCollectZipUsesLeftAllocationAndDropsTails) {
    auto left = Vec<int>::with_capacity(usize(8));
    for (int value = 0; value < 5; ++value) left.push(rstd::move(value));
    auto* left_allocation = left.data();

    auto right = Vec<int>::make();
    right.push(10);
    right.push(20);
    right.push(30);

    auto values = left.into_iter()
                      .zip(right.into_iter())
                      .map([](auto pair) {
                          return pair.template get<0>() + pair.template get<1>();
                      })
                      .collect<Vec<int>>();

    EXPECT_EQ(values.data(), left_allocation);
    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()], 10);
    EXPECT_EQ(values[usize(1)], 21);
    EXPECT_EQ(values[usize(2)], 32);
}

TEST(Vec, InPlaceCollectStatefulAdaptersPreserveStopSemantics) {
    int drops = 0;
    {
        auto source = Vec<CollectMoveOnly>::with_capacity(usize(8));
        for (int value = 0; value < 8; ++value) source.emplace_back(value, drops);
        auto* allocation = source.data();

        auto values = source.into_iter()
                          .skip(usize(1))
                          .skip_while([](const CollectMoveOnly& value) {
                              return value.value < 3;
                          })
                          .take_while([](const CollectMoveOnly& value) {
                              return value.value < 7;
                          })
                          .map_while([](CollectMoveOnly value) -> rstd::Option<CollectMoveOnly> {
                              if (value.value == 5) return rstd::None();
                              return rstd::Some(rstd::move(value));
                          })
                          .collect<Vec<CollectMoveOnly>>();

        EXPECT_EQ(values.data(), allocation);
        ASSERT_EQ(values.len(), usize(2));
        EXPECT_EQ(values[usize()].value, 3);
        EXPECT_EQ(values[usize(1)].value, 4);
        EXPECT_EQ(drops, 6);
    }
    EXPECT_EQ(drops, 8);
}

TEST(Vec, InPlaceCollectEnumerateFilterMapAndScanReuseAllocation) {
    auto source = Vec<int>::with_capacity(usize(8));
    for (int value = 1; value <= 6; ++value) source.push(rstd::move(value));
    auto* allocation = source.data();

    auto values = source.into_iter()
                      .enumerate()
                      .filter_map([](auto indexed) -> rstd::Option<int> {
                          auto index = indexed.template get<0>();
                          auto value = indexed.template get<1>();
                          if (index.to_primitive() % 2 != 0) return rstd::None();
                          return rstd::Some(value);
                      })
                      .scan(0,
                            [](int& total, int value) -> rstd::Option<int> {
                                total += value;
                                return rstd::Some(total);
                            })
                      .collect<Vec<int>>();

    EXPECT_EQ(values.data(), allocation);
    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()], 1);
    EXPECT_EQ(values[usize(1)], 4);
    EXPECT_EQ(values[usize(2)], 9);
}

TEST(Vec, InPlaceCollectCopiedAndClonedPropagateOwningSource) {
    rstd::u64 backing[] { rstd::u64(3), rstd::u64(5), rstd::u64(8) };

    auto copied_source = Vec<rstd::ref<rstd::u64>>::with_capacity(usize(4));
    for (auto& value : backing) {
        copied_source.push(rstd::ref<rstd::u64>::from_raw_parts(&value));
    }
    auto* copied_allocation = copied_source.data();
    auto  copied            = copied_source.into_iter().copied().collect<Vec<rstd::u64>>();

    EXPECT_EQ(static_cast<void*>(copied.data()), static_cast<void*>(copied_allocation));
    ASSERT_EQ(copied.len(), usize(3));
    EXPECT_EQ(copied[usize()], rstd::u64(3));
    EXPECT_EQ(copied[usize(2)], rstd::u64(8));

    auto cloned_source = Vec<rstd::ref<rstd::u64>>::with_capacity(usize(4));
    for (auto& value : backing) {
        cloned_source.push(rstd::ref<rstd::u64>::from_raw_parts(&value));
    }
    auto* cloned_allocation = cloned_source.data();
    auto  cloned            = cloned_source.into_iter().cloned().collect<Vec<rstd::u64>>();

    EXPECT_EQ(static_cast<void*>(cloned.data()), static_cast<void*>(cloned_allocation));
    ASSERT_EQ(cloned.len(), usize(3));
    EXPECT_EQ(cloned[usize(1)], rstd::u64(5));
}

TEST(Vec, InPlaceCollectFallsBackForIncompatibleLayout) {
    auto source = Vec<int>::with_capacity(usize(5));
    source.push(3);
    source.push(5);
    source.push(8);
    auto* allocation = source.data();

    auto values = source.into_iter()
                      .map([](int value) {
                          return InPlaceAlignedLong { value };
                      })
                      .collect<Vec<InPlaceAlignedLong>>();

    EXPECT_NE(static_cast<void*>(values.data()), static_cast<void*>(allocation));
    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()].value, 3);
    EXPECT_EQ(values[usize(2)].value, 8);
}

TEST(Vec, InPlaceCollectFallsBackForPeekable) {
    auto source = Vec<int>::with_capacity(usize(6));
    source.push(2);
    source.push(4);
    source.push(6);
    auto* allocation = source.data();

    auto values = source.into_iter().peekable().collect<Vec<int>>();

    EXPECT_NE(values.data(), allocation);
    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()], 2);
    EXPECT_EQ(values[usize(1)], 4);
    EXPECT_EQ(values[usize(2)], 6);
}

TEST(Vec, InPlaceCollectUnsupportedAdaptersRemainCollectible) {
    {
        auto source = Vec<int>::with_capacity(usize(6));
        source.push(1);
        source.push(2);
        source.push(3);
        auto* allocation = source.data();
        auto  values     = source.into_iter().fuse().collect<Vec<int>>();
        EXPECT_NE(values.data(), allocation);
        EXPECT_EQ(values.as_slice(), (rstd::array<int, 3> { 1, 2, 3 }.as_slice()));
    }
    {
        auto source = Vec<int>::with_capacity(usize(6));
        for (int value = 0; value < 5; ++value) source.push(rstd::move(value));
        auto* allocation = source.data();
        auto  values     = source.into_iter().step_by(usize(2)).collect<Vec<int>>();
        EXPECT_NE(values.data(), allocation);
        EXPECT_EQ(values.as_slice(), (rstd::array<int, 3> { 0, 2, 4 }.as_slice()));
    }
    {
        auto source = Vec<int>::with_capacity(usize(6));
        source.push(1);
        source.push(2);
        source.push(3);
        auto* allocation = source.data();
        auto  values     = source.into_iter().rev().collect<Vec<int>>();
        EXPECT_NE(values.data(), allocation);
        EXPECT_EQ(values.as_slice(), (rstd::array<int, 3> { 3, 2, 1 }.as_slice()));
    }
    {
        auto first = Vec<int>::with_capacity(usize(4));
        first.push(1);
        first.push(2);
        auto* first_allocation = first.data();
        auto  second           = Vec<int>::make();
        second.push(3);
        second.push(4);
        auto values = first.into_iter().chain(second.into_iter()).collect<Vec<int>>();
        EXPECT_NE(values.data(), first_allocation);
        EXPECT_EQ(values.as_slice(), (rstd::array<int, 4> { 1, 2, 3, 4 }.as_slice()));
    }
    {
        auto source = Vec<int>::with_capacity(usize(4));
        source.push(1);
        source.push(2);
        source.push(3);
        auto* allocation = source.data();
        auto  values     = source.into_iter().intersperse(0).collect<Vec<int>>();
        EXPECT_NE(values.data(), allocation);
        EXPECT_EQ(values.as_slice(), (rstd::array<int, 5> { 1, 0, 2, 0, 3 }.as_slice()));
    }
    {
        auto source = Vec<Vec<int>>::with_capacity(usize(3));
        for (int value = 1; value <= 3; ++value) {
            auto inner = Vec<int>::make();
            inner.push(rstd::move(value));
            source.push(rstd::move(inner));
        }
        auto* allocation = source.data();
        auto  values     = source.into_iter().flatten().collect<Vec<int>>();
        EXPECT_NE(static_cast<void*>(values.data()), static_cast<void*>(allocation));
        EXPECT_EQ(values.as_slice(), (rstd::array<int, 3> { 1, 2, 3 }.as_slice()));
    }
    {
        auto source = Vec<int>::with_capacity(usize(3));
        source.push(1);
        source.push(2);
        auto* allocation = source.data();
        auto  values     = source.into_iter()
                               .flat_map([](int value) {
                              auto inner = Vec<int>::make();
                              inner.emplace_back(value);
                              inner.emplace_back(-value);
                              return inner;
                               })
                               .collect<Vec<int>>();
        EXPECT_NE(values.data(), allocation);
        EXPECT_EQ(values.as_slice(), (rstd::array<int, 4> { 1, -1, 2, -2 }.as_slice()));
    }
}

TEST(Vec, InPlaceCollectUsesByteStorageForU8Conversions) {
    auto bytes = Vec<u8>::with_capacity(usize(8));
    bytes.push(u8(3));
    bytes.push(u8(5));
    bytes.push(u8(8));
    auto* allocation = bytes.data();

    auto raw = bytes.into_iter()
                   .map([](u8 value) {
                       return value.to_byte();
                   })
                   .collect<Vec<rstd::byte>>();

    EXPECT_EQ(raw.data(), allocation);
    EXPECT_EQ(raw[usize()], rstd::byte { 3 });
    EXPECT_EQ(raw[usize(1)], rstd::byte { 5 });
    EXPECT_EQ(raw[usize(2)], rstd::byte { 8 });
    auto* raw_allocation = raw.data();

    auto logical = raw.into_iter()
                       .map([](rstd::byte value) {
                           return u8::from_byte(value);
                       })
                       .collect<Vec<u8>>();

    EXPECT_EQ(logical.data(), raw_allocation);
    EXPECT_EQ(logical.as_slice(), "\x03\x05\x08"_bytes);
}

TEST(Vec, Indexing) {
    Vec<int> v;
    v.push(10);
    v.push(20);

    EXPECT_EQ(v[usize()], 10);
    EXPECT_EQ(v[usize(1)], 20);

    v[usize()] = 30;
    EXPECT_EQ(v[usize()], 30);
}

TEST(Vec, Destructor) {
    static int drop_count = 0;
    struct Dropper {
        Dropper()                          = default;
        Dropper(const Dropper&)            = delete;
        Dropper& operator=(const Dropper&) = delete;
        Dropper(Dropper&&) noexcept {}
        Dropper& operator=(Dropper&&) noexcept { return *this; }
        ~Dropper() { drop_count++; }
    };

    drop_count = 0;
    {
        Vec<Dropper> v;
        v.push(Dropper {});
        v.push(Dropper {});
    }
    // Each push involves a temporary and a move.
    // 2 (temporaries) + 2 (in vec) = 4 drops.
    EXPECT_EQ(drop_count, 4);
}

TEST(Vec, IntoBoxedSlice) {
    Vec<int> v;
    v.push(1);
    v.push(2);

    auto b = v.into_boxed_slice();
    EXPECT_EQ(v.len(), usize());
    // Box<T[]> should have metadata for length
    EXPECT_EQ(b.as_ptr().len(), usize(2));
    EXPECT_EQ(b.as_ptr()[usize()], 1);
    EXPECT_EQ(b.as_ptr()[usize(1)], 2);
    EXPECT_EQ(rstd::alloc::Layout::for_value(b.as_ptr()).size, usize(2 * sizeof(int)));
}

TEST(Vec, BoxedSliceRoundTripTransfersExactAllocation) {
    auto values = Vec<rstd::u8>::with_capacity(usize(3));
    values.push(u8(3));
    values.push(u8(5));
    values.push(u8(8));
    auto* allocation = values.data();

    auto boxed = values.into_boxed_slice();
    EXPECT_EQ(boxed.get(), allocation);

    auto round_trip = Vec<rstd::u8>::from_boxed_slice(rstd::move(boxed));
    EXPECT_EQ(round_trip.data(), allocation);
    EXPECT_EQ(round_trip.capacity(), usize(3));
    EXPECT_EQ(round_trip.as_slice(), "\x03\x05\x08"_bytes);
}

TEST(Vec, EmptyBoxedSliceRoundTripStaysEmpty) {
    auto boxed = Vec<int>::make().into_boxed_slice();
    EXPECT_EQ(boxed.as_ptr().len(), usize());

    auto values = Vec<int>::from_boxed_slice(rstd::move(boxed));
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(values.capacity(), usize());
}

TEST(Vec, BoxedSliceDropsEveryElement) {
    struct DropProbe {
        int* drops;

        explicit DropProbe(int& drops): drops(&drops) {}
        DropProbe(const DropProbe&)            = delete;
        DropProbe& operator=(const DropProbe&) = delete;

        DropProbe(DropProbe&& other) noexcept: drops(other.drops) { other.drops = nullptr; }

        ~DropProbe() {
            if (drops != nullptr) ++*drops;
        }
    };

    int  drops  = 0;
    auto values = Vec<DropProbe>::make();
    values.push(DropProbe { drops });
    values.push(DropProbe { drops });

    {
        auto boxed = values.into_boxed_slice();
        EXPECT_EQ(drops, 0);
    }
    EXPECT_EQ(drops, 2);
}

TEST(Vec, Remove) {
    Vec<int> v;
    v.push(1);
    v.push(2);
    v.push(3);

    EXPECT_EQ(v.remove(usize(1)), 2);
    EXPECT_EQ(v.len(), usize(2));
    EXPECT_EQ(v[usize()], 1);
    EXPECT_EQ(v[usize(1)], 3);
}

TEST(Vec, RetainPreservesOrderAndDropsRemovedElements) {
    struct Tracked {
        int* drops;
        int  value;

        Tracked(int& count, int number): drops(&count), value(number) {}
        Tracked(const Tracked&)            = delete;
        Tracked& operator=(const Tracked&) = delete;
        Tracked(Tracked&& other) noexcept: drops(other.drops), value(other.value) {
            other.drops = nullptr;
        }
        Tracked& operator=(Tracked&&) = delete;
        ~Tracked() {
            if (drops != nullptr) ++*drops;
        }
    };

    int  drops  = 0;
    auto values = Vec<Tracked>::make();
    for (int value = 0; value < 6; ++value) values.emplace_back(drops, value);

    values.retain([](const Tracked& value) {
        return value.value % 2 == 0;
    });

    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()].value, 0);
    EXPECT_EQ(values[usize(1)].value, 2);
    EXPECT_EQ(values[usize(2)].value, 4);
    EXPECT_EQ(drops, 3);

    values.retain([](const Tracked&) {
        return false;
    });
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(drops, 6);
}

TEST(Vec, ReserveAndExtendFromSlice) {
    Vec<rstd::u8> v;
    v.reserve(usize(8));
    EXPECT_GE(v.capacity(), usize(8));
    EXPECT_EQ(v.len(), usize());

    auto data = rstd::array<rstd::u8, 3> { u8(1), u8(2), u8(3) };
    v.extend_from_slice(data.as_slice());

    EXPECT_EQ(v.len(), usize(3));
    EXPECT_EQ(v[usize()], u8(1));
    EXPECT_EQ(v[usize(1)], u8(2));
    EXPECT_EQ(v[usize(2)], u8(3));
}

TEST(Vec, U8GrowthKeepsByteStorageAndProxyWrites) {
    auto values = Vec<rstd::u8>::with_capacity(usize(1));
    for (rstd::uint16_t value = 0; value < 256; ++value) {
        values.push(rstd::u8(static_cast<rstd::uint8_t>(value)));
    }

    ASSERT_EQ(values.len(), usize(256));
    static_assert(rstd::mtp::same_as<decltype(values[usize()]), rstd::mut_ref<rstd::u8>>);
    values[usize(128)] = rstd::u8(17);
    EXPECT_EQ(values[usize(128)], rstd::u8(17));
    EXPECT_EQ(values.data()[128], rstd::byte { 17 });

    auto boxed = values.into_boxed_slice();
    static_assert(rstd::mtp::same_as<decltype(boxed.get()), rstd::byte*>);
    EXPECT_EQ(boxed.as_ptr()[usize(128)], rstd::u8(17));
}

TEST(Vec, FromSliceOwnsIndependentCopy) {
    int  source[] { 3, 5, 8 };
    auto source_slice = rstd::slice<int>::from_raw_parts(source, usize(3));
    auto values = rstd::Impl<rstd::convert::From<rstd::slice<int>>, Vec<int>>::from(source_slice);

    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values.capacity(), usize(3));
    EXPECT_EQ(values[usize()], 3);
    EXPECT_EQ(values[usize(1)], 5);
    EXPECT_EQ(values[usize(2)], 8);

    source[1]        = 13;
    values[usize(2)] = 21;
    EXPECT_EQ(values[usize(1)], 5);
    EXPECT_EQ(source[2], 8);
}

TEST(Vec, FromEmptySliceDoesNotAllocate) {
    auto values = Vec<int>::from(rstd::slice<int>());
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(values.capacity(), usize());
}

TEST(Vec, FromSliceSupportsCloneOnlyElements) {
    int          clones      = 0;
    int          clone_froms = 0;
    CloneTracked source[] { CloneTracked { 2, clones, clone_froms },
                            CloneTracked { 7, clones, clone_froms } };

    auto values =
        Vec<CloneTracked>::from(rstd::slice<CloneTracked>::from_raw_parts(source, usize(2)));

    ASSERT_EQ(values.len(), usize(2));
    EXPECT_EQ(values[usize()].value, 2);
    EXPECT_EQ(values[usize(1)].value, 7);
    EXPECT_EQ(clones, 2);
    EXPECT_EQ(clone_froms, 0);
}

TEST(Vec, ExtendFromOwnSliceSurvivesGrowth) {
    auto values = Vec<int>::with_capacity(usize(2));
    values.push(1);
    values.push(2);

    values.extend_from_slice(values.as_slice());

    ASSERT_EQ(values.len(), usize(4));
    EXPECT_EQ(values[usize()], 1);
    EXPECT_EQ(values[usize(1)], 2);
    EXPECT_EQ(values[usize(2)], 1);
    EXPECT_EQ(values[usize(3)], 2);
}

TEST(Vec, ExtendFromPointerClonesWithoutGrowth) {
    int          clones      = 0;
    int          clone_froms = 0;
    CloneTracked source[] { CloneTracked { 3, clones, clone_froms },
                            CloneTracked { 5, clones, clone_froms } };
    auto         values     = Vec<CloneTracked>::with_capacity(usize(3));
    auto*        allocation = values.data();

    values.extend_from_slice(source, usize(2));

    EXPECT_EQ(values.data(), allocation);
    ASSERT_EQ(values.len(), usize(2));
    EXPECT_EQ(values[usize()].value, 3);
    EXPECT_EQ(values[usize(1)].value, 5);
    EXPECT_EQ(clones, 2);
    EXPECT_EQ(clone_froms, 0);
}

TEST(Vec, CloneFromReusesCapacityAndInitializedElements) {
    int clones      = 0;
    int clone_froms = 0;

    auto source = Vec<CloneTracked>::make();
    source.push(CloneTracked { 4, clones, clone_froms });
    source.push(CloneTracked { 9, clones, clone_froms });
    source.push(CloneTracked { 16, clones, clone_froms });

    auto target = Vec<CloneTracked>::with_capacity(usize(4));
    target.push(CloneTracked { 1, clones, clone_froms });
    target.push(CloneTracked { 2, clones, clone_froms });
    auto* allocation = target.data();

    const auto& const_source = source;
    target.clone_from(const_source);

    EXPECT_EQ(target.data(), allocation);
    ASSERT_EQ(target.len(), usize(3));
    EXPECT_EQ(target[usize()].value, 4);
    EXPECT_EQ(target[usize(1)].value, 9);
    EXPECT_EQ(target[usize(2)].value, 16);
    EXPECT_EQ(clone_froms, 2);
    EXPECT_EQ(clones, 1);
}

TEST(Vec, CloneFromTruncatesLongerTarget) {
    int clones      = 0;
    int clone_froms = 0;

    auto source = Vec<CloneTracked>::make();
    source.push(CloneTracked { 6, clones, clone_froms });

    auto target = Vec<CloneTracked>::with_capacity(usize(3));
    target.push(CloneTracked { 1, clones, clone_froms });
    target.push(CloneTracked { 2, clones, clone_froms });
    target.push(CloneTracked { 3, clones, clone_froms });

    target.clone_from(source);

    ASSERT_EQ(target.len(), usize(1));
    EXPECT_EQ(target[usize()].value, 6);
    EXPECT_EQ(clone_froms, 1);
    EXPECT_EQ(clones, 0);
}

TEST(Vec, CloneFromEqualLengthReusesEveryElement) {
    int clones      = 0;
    int clone_froms = 0;

    auto source = Vec<CloneTracked>::make();
    source.push(CloneTracked { 8, clones, clone_froms });
    source.push(CloneTracked { 13, clones, clone_froms });

    auto target = Vec<CloneTracked>::with_capacity(usize(2));
    target.push(CloneTracked { 1, clones, clone_froms });
    target.push(CloneTracked { 2, clones, clone_froms });
    auto* allocation = target.data();

    target.clone_from(source);

    EXPECT_EQ(target.data(), allocation);
    ASSERT_EQ(target.len(), usize(2));
    EXPECT_EQ(target[usize()].value, 8);
    EXPECT_EQ(target[usize(1)].value, 13);
    EXPECT_EQ(clone_froms, 2);
    EXPECT_EQ(clones, 0);
}

TEST(Vec, SpareCapacityAndSetLen) {
    auto v = Vec<rstd::u8>::with_capacity(usize(4));

    auto spare = v.spare_capacity_mut();
    ASSERT_EQ(spare.len(), usize(4));
    spare[usize()].write(u8(7));
    spare[usize(1)].write(u8(8));
    spare[usize(2)].write(u8(9));
    v.set_len_unchecked(usize(3));

    EXPECT_EQ(v.len(), usize(3));
    EXPECT_EQ(v[usize()], u8(7));
    EXPECT_EQ(v[usize(1)], u8(8));
    EXPECT_EQ(v[usize(2)], u8(9));

    v.truncate(usize(2));
    EXPECT_EQ(v.len(), usize(2));
    EXPECT_EQ(v[usize(1)], u8(8));

    EXPECT_DEATH(v.spare_capacity_mut()[usize(2)].write(u8(1)),
                 "Vec spare capacity index out of bounds");
}

TEST(Vec, SpareCapacityConstructsNonTrivialElements) {
    struct Tracked {
        int* drops;
        int  value;

        Tracked(int& count, int number): drops(&count), value(number) {}
        Tracked(const Tracked&)            = delete;
        Tracked& operator=(const Tracked&) = delete;
        Tracked(Tracked&& other) noexcept: drops(other.drops), value(other.value) {
            other.drops = nullptr;
        }
        ~Tracked() {
            if (drops != nullptr) ++*drops;
        }
    };

    int drops = 0;
    {
        auto values = Vec<Tracked>::with_capacity(usize(1));
        values.spare_capacity_mut()[usize()].write(Tracked(drops, 7));
        values.set_len_unchecked(usize(1));
        EXPECT_EQ(values[usize()].value, 7);
        EXPECT_EQ(drops, 0);
    }
    EXPECT_EQ(drops, 1);
}

TEST(Vec, Resize) {
    Vec<int> v;
    v.resize(usize(3), 5);

    ASSERT_EQ(v.len(), usize(3));
    EXPECT_EQ(v[usize()], 5);
    EXPECT_EQ(v[usize(1)], 5);
    EXPECT_EQ(v[usize(2)], 5);

    v.resize(usize(1), 0);
    EXPECT_EQ(v.len(), usize(1));
    EXPECT_EQ(v[usize()], 5);
}

TEST(Vec, ResizeU8FillsNewRange) {
    auto values = Vec<u8>::make();
    values.push(u8(1));
    values.resize(usize(4096), u8(0xab));

    ASSERT_EQ(values.len(), usize(4096));
    EXPECT_EQ(values[usize()], u8(1));
    EXPECT_EQ(values[usize(1)], u8(0xab));
    EXPECT_EQ(values[usize(4095)], u8(0xab));

    values.truncate(usize(2));
    values.resize(usize(16), u8(0x5a));
    ASSERT_EQ(values.len(), usize(16));
    EXPECT_EQ(values[usize(1)], u8(0xab));
    EXPECT_EQ(values[usize(2)], u8(0x5a));
    EXPECT_EQ(values[usize(15)], u8(0x5a));
}

TEST(Vec, ResizeFromOwnElementSurvivesGrowth) {
    auto values = Vec<int>::with_capacity(usize(1));
    values.push(17);

    values.resize(usize(3), values[usize()]);

    ASSERT_EQ(values.len(), usize(3));
    EXPECT_EQ(values[usize()], 17);
    EXPECT_EQ(values[usize(1)], 17);
    EXPECT_EQ(values[usize(2)], 17);
}

TEST(Vec, CloneOwnsIndependentElements) {
    auto values = Vec<String>::make();
    values.push(String::make("alpha"_str));
    values.push(String::make("beta"_str));

    auto direct   = values.clone();
    auto abstract = rstd::as<rstd::clone::Clone>(values).clone();
    values[usize()].push_ascii(u8('!'));

    ASSERT_EQ(direct.len(), usize(2));
    ASSERT_EQ(abstract.len(), usize(2));
    EXPECT_EQ(values[usize()], "alpha!"_str);
    EXPECT_EQ(direct[usize()], "alpha"_str);
    EXPECT_EQ(direct[usize(1)], "beta"_str);
    EXPECT_EQ(abstract[usize()], "alpha"_str);
    EXPECT_EQ(abstract[usize(1)], "beta"_str);
}

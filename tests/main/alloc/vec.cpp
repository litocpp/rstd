#include <rstd/test/gtest.hpp>
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

template<typename T>
concept MutableVecEndpoints = requires(T& value) {
    value.first_mut();
    value.last_mut();
};
static_assert(MutableVecEndpoints<Vec<int>>);
static_assert(! MutableVecEndpoints<const Vec<int>>);

TEST(Vec, EndpointsBorrowWithoutRemovingElements) {
    Vec<Box<int>> values;
    EXPECT_TRUE(values.first().is_none());
    EXPECT_TRUE(values.last().is_none());
    EXPECT_TRUE(values.first_mut().is_none());
    EXPECT_TRUE(values.last_mut().is_none());
    values.push(Box<int>::make(3));
    EXPECT_EQ(values.first()->as_raw_ptr(), values.last()->as_raw_ptr());
    values.push(Box<int>::make(5));
    const auto& readonly = values;
    EXPECT_EQ(readonly.first()->as_raw_ptr(), values.data());
    EXPECT_EQ(readonly.last()->as_raw_ptr(), values.data() + 1);
    *values.last_mut()->get_mut() = 9;
    EXPECT_EQ(*readonly.last()->get(), 9);
    EXPECT_EQ(values.len(), 2_usize);
}

TEST(Vec, ByteEndpointsPreserveProxyStorage) {
    Vec<u8> values;
    EXPECT_TRUE(values.last_mut().is_none());
    values.push(u8(1));
    values.push(u8(2));
    auto first = values.first_mut().unwrap();
    auto last  = values.last_mut().unwrap();
    first      = u8(7);
    last       = u8(9);
    EXPECT_EQ(values.first()->get(), u8(7));
    EXPECT_EQ(values.last()->get(), u8(9));
    EXPECT_EQ(first.as_raw_ptr(), values.data());
    EXPECT_EQ(last.as_raw_ptr(), values.data() + 1);
    EXPECT_EQ(values.len(), 2_usize);
}

struct VecAllocatorState {
    int  allocations;
    int  deallocations;
    bool fail {};
};

struct VecTestAllocator {
    VecAllocatorState* state;
};

namespace rstd
{

template<>
struct Impl<alloc::Allocator, ::VecTestAllocator>
    : DefaultInImpl<alloc::Allocator, ::VecTestAllocator> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        if (this->self().state->fail) return Err(alloc::AllocError {});
        ++this->self().state->allocations;
        return as<alloc::Allocator>(::alloc::GLOBAL).allocate(layout);
    }

    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        ++this->self().state->deallocations;
        as<alloc::Allocator>(::alloc::GLOBAL).deallocate(pointer, layout);
    }
};

} // namespace rstd

static_assert(sizeof(Vec<int>) == sizeof(void*) * 5);

TEST(Vec, FailedReservationPreservesAllocationAndElements) {
    VecAllocatorState state {};
    auto              value =
        Vec<int, VecTestAllocator>::with_capacity_in(rstd::usize(2), VecTestAllocator { &state });
    value.push(7);
    value.push(9);
    auto pointer = value.data();
    state.fail   = true;
    auto result  = value.try_reserve_exact(rstd::usize(20));
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err(), ::alloc::collections::TryReserveError::AllocError);
    EXPECT_EQ(value.data(), pointer);
    EXPECT_EQ(value.len(), rstd::usize(2));
    EXPECT_EQ(value[rstd::usize()], 7);
    EXPECT_EQ(value[rstd::usize(1)], 9);
    state.fail = false;
    EXPECT_TRUE(value.try_reserve(rstd::usize(20)).is_ok());
    value.shrink_to(rstd::usize(4));
    EXPECT_EQ(value.len(), rstd::usize(2));
    EXPECT_GE(value.capacity(), rstd::usize(4));
}

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
    for (usize n; n < 4_usize; ++n) {
        auto bytes = rstd::iter::repeat_with([] {
                         return u8(7);
                     })
                         .take(n)
                         .collect<Vec<u8>>();
        EXPECT_EQ(bytes.len(), n);
        EXPECT_EQ(bytes.capacity(), n);
    }
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

    auto values = rstd::move(source).into_iter().collect<Vec<int>>();

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
        auto  iterator   = rstd::move(source).into_iter();
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
        auto  iterator   = rstd::move(source).into_iter();
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
    auto  iterator   = rstd::move(source).into_iter();
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
        auto iterator = rstd::move(source).into_iter();
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

    auto values = rstd::move(source)
                      .into_iter()
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
    auto  iterator   = rstd::move(source).into_iter();
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

    auto values = rstd::move(source)
                      .into_iter()
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

    auto values = rstd::move(source)
                      .into_iter()
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

    auto values = rstd::move(source)
                      .into_iter()
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

    auto values = rstd::move(source)
                      .into_iter()
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
            rstd::move(source)
                .into_iter()
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

        auto values = rstd::move(source).into_iter().take(usize(2)).collect<Vec<CollectMoveOnly>>();

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

    auto values = rstd::move(left)
                      .into_iter()
                      .zip(rstd::move(right).into_iter())
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

        auto values = rstd::move(source)
                          .into_iter()
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

    auto values = rstd::move(source)
                      .into_iter()
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
    auto  copied = rstd::move(copied_source).into_iter().copied().collect<Vec<rstd::u64>>();

    EXPECT_EQ(static_cast<void*>(copied.data()), static_cast<void*>(copied_allocation));
    ASSERT_EQ(copied.len(), usize(3));
    EXPECT_EQ(copied[usize()], rstd::u64(3));
    EXPECT_EQ(copied[usize(2)], rstd::u64(8));

    auto cloned_source = Vec<rstd::ref<rstd::u64>>::with_capacity(usize(4));
    for (auto& value : backing) {
        cloned_source.push(rstd::ref<rstd::u64>::from_raw_parts(&value));
    }
    auto* cloned_allocation = cloned_source.data();
    auto  cloned = rstd::move(cloned_source).into_iter().cloned().collect<Vec<rstd::u64>>();

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

    auto values = rstd::move(source)
                      .into_iter()
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

    auto values = rstd::move(source).into_iter().peekable().collect<Vec<int>>();

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
        auto  values     = rstd::move(source).into_iter().fuse().collect<Vec<int>>();
        EXPECT_NE(values.data(), allocation);
        EXPECT_EQ(values.as_slice(), (rstd::array<int, 3> { 1, 2, 3 }.as_slice()));
    }
    {
        auto source = Vec<int>::with_capacity(usize(6));
        for (int value = 0; value < 5; ++value) source.push(rstd::move(value));
        auto* allocation = source.data();
        auto  values     = rstd::move(source).into_iter().step_by(usize(2)).collect<Vec<int>>();
        EXPECT_NE(values.data(), allocation);
        EXPECT_EQ(values.as_slice(), (rstd::array<int, 3> { 0, 2, 4 }.as_slice()));
    }
    {
        auto source = Vec<int>::with_capacity(usize(6));
        source.push(1);
        source.push(2);
        source.push(3);
        auto* allocation = source.data();
        auto  values     = rstd::move(source).into_iter().rev().collect<Vec<int>>();
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
        auto values =
            rstd::move(first).into_iter().chain(rstd::move(second).into_iter()).collect<Vec<int>>();
        EXPECT_NE(values.data(), first_allocation);
        EXPECT_EQ(values.as_slice(), (rstd::array<int, 4> { 1, 2, 3, 4 }.as_slice()));
    }
    {
        auto source = Vec<int>::with_capacity(usize(4));
        source.push(1);
        source.push(2);
        source.push(3);
        auto* allocation = source.data();
        auto  values     = rstd::move(source).into_iter().intersperse(0).collect<Vec<int>>();
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
        auto  values     = rstd::move(source).into_iter().flatten().collect<Vec<int>>();
        EXPECT_NE(static_cast<void*>(values.data()), static_cast<void*>(allocation));
        EXPECT_EQ(values.as_slice(), (rstd::array<int, 3> { 1, 2, 3 }.as_slice()));
    }
    {
        auto source = Vec<int>::with_capacity(usize(3));
        source.push(1);
        source.push(2);
        auto* allocation = source.data();
        auto  values     = rstd::move(source)
                               .into_iter()
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

    auto raw = rstd::move(bytes)
                   .into_iter()
                   .map([](u8 value) {
                       return value.to_byte();
                   })
                   .collect<Vec<rstd::byte>>();

    EXPECT_EQ(raw.data(), allocation);
    EXPECT_EQ(raw[usize()], rstd::byte { 3 });
    EXPECT_EQ(raw[usize(1)], rstd::byte { 5 });
    EXPECT_EQ(raw[usize(2)], rstd::byte { 8 });
    auto* raw_allocation = raw.data();

    auto logical = rstd::move(raw)
                       .into_iter()
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

TEST(Vec, InsertPreservesOrderAtEveryPosition) {
    for (int returning = 0; returning < 2; ++returning) {
        for (rstd::size_t length = 0; length <= 3; ++length) {
            for (rstd::size_t position = 0; position <= length; ++position) {
                for (rstd::size_t spare = 0; spare <= 1; ++spare) {
                    VecAllocatorState state {};
                    {
                        auto values = Vec<int, VecTestAllocator>::with_capacity_in(
                            usize(length + spare), VecTestAllocator { &state });
                        for (rstd::size_t i = 0; i < length; ++i) values.push(static_cast<int>(i));
                        auto allocations = state.allocations;
                        if (returning) {
                            auto inserted = values.insert_mut(usize(position), 41);
                            static_assert(rstd::mtp::same_as<decltype(inserted), mut_ref<int>>);
                            EXPECT_EQ(inserted.as_raw_ptr(), values.data() + position);
                            inserted.get_mut() = 42;
                        } else {
                            values.insert(usize(position), 42);
                        }
                        ASSERT_EQ(values.len(), usize(length + 1));
                        if (spare != 0)
                            EXPECT_EQ(state.allocations, allocations);
                        else
                            EXPECT_GT(state.allocations, allocations);
                        for (rstd::size_t i = 0; i <= length; ++i) {
                            auto expected =
                                i == position ? 42 : static_cast<int>(i < position ? i : i - 1);
                            EXPECT_EQ(values[usize(i)], expected);
                        }
                    }
                    EXPECT_EQ(state.allocations, state.deallocations);
                }
            }
        }
    }
}

TEST(Vec, InsertMovesWithoutAssignmentAndPreservesResources) {
    for (rstd::size_t spare = 0; spare <= 1; ++spare) {
        int drops = 0;
        {
            auto values = Vec<CollectMoveOnly>::with_capacity(usize(2 + spare));
            values.emplace_back(10, drops);
            values.emplace_back(30, drops);
            values.insert(1_usize, CollectMoveOnly(20, drops));
            values.insert(0_usize, CollectMoveOnly(0, drops));
            ASSERT_EQ(values.len(), 4_usize);
            EXPECT_EQ(drops, 0);
            for (rstd::size_t i = 0; i < 4; ++i) {
                auto& element = values.data()[i];
                EXPECT_EQ(element.value, static_cast<int>(i) * 10);
                EXPECT_EQ(element.value_ptr, &element.value);
            }
        }
        EXPECT_EQ(drops, 4);
    }
}

TEST(Vec, InsertOwnsAliasedInputBeforeGrowthOrShifting) {
    for (rstd::size_t spare = 0; spare <= 1; ++spare) {
        for (rstd::size_t position = 0; position <= 3; ++position) {
            for (rstd::size_t source = 0; source < 3; ++source) {
                int drops = 0;
                {
                    auto values = Vec<CollectMoveOnly>::with_capacity(usize(3 + spare));
                    for (int i = 0; i < 3; ++i) values.emplace_back(i, drops);
                    auto inserted =
                        values.insert_mut(usize(position), rstd::move(values.data()[source]));
                    ASSERT_EQ(values.len(), 4_usize);
                    EXPECT_EQ(inserted.as_raw_ptr(), values.data() + position);
                    EXPECT_EQ(inserted.get().value, static_cast<int>(source));
                    EXPECT_EQ(inserted.get().drops, &drops);
                    auto moved_from = source < position ? source : source + 1;
                    for (rstd::size_t i = 0; i < 4; ++i) {
                        auto& element = values.data()[i];
                        EXPECT_EQ(element.value_ptr, &element.value);
                        EXPECT_EQ(element.drops, i == moved_from ? nullptr : &drops);
                        auto expected = i == position ? source : (i < position ? i : i - 1);
                        EXPECT_EQ(element.value, static_cast<int>(expected));
                    }
                    EXPECT_EQ(drops, 0);
                }
                EXPECT_EQ(drops, 3);
            }
        }
    }
}

TEST(Vec, InsertU8PreservesByteStorageAndReturnsWritableProxy) {
    for (rstd::size_t spare = 0; spare <= 1; ++spare) {
        auto values = Vec<u8>::with_capacity(usize(2 + spare));
        values.push(u8(1));
        values.push(u8(3));
        auto inserted = values.insert_mut(1_usize, u8(2));
        static_assert(rstd::mtp::same_as<decltype(inserted), mut_ref<u8>>);
        EXPECT_EQ(inserted.as_raw_ptr(), values.data() + 1);
        inserted = u8(9);
        values.insert(0_usize, u8(0));
        values.insert(values.len(), u8(4));
        ASSERT_EQ(values.len(), 5_usize);
        const unsigned char expected[] { 0, 1, 9, 3, 4 };
        for (rstd::size_t i = 0; i < 5; ++i) {
            EXPECT_EQ(values[usize(i)], u8(expected[i]));
            EXPECT_EQ(values.data()[i], rstd::byte { expected[i] });
        }
    }
    Vec<u8> empty;
    empty.insert_mut(0_usize, u8(7)) = u8(8);
    EXPECT_EQ(empty[0_usize], u8(8));
}

TEST(Vec, InsertRejectsOutOfBoundsBeforeMovingInput) {
    struct MoveProbe {
        MoveProbe() = default;
        MoveProbe(MoveProbe&&) { rstd::panic { "input moved before bounds check" }; }
    };
    Vec<MoveProbe> empty;
    MoveProbe      input;
    EXPECT_DEATH(empty.insert(1_usize, rstd::move(input)),
                 "Vec insertion index 1 exceeds length 0");
    EXPECT_DEATH((void)empty.insert_mut(1_usize, rstd::move(input)),
                 "Vec insertion index 1 exceeds length 0");

    VecAllocatorState state {};
    auto              values =
        Vec<int, VecTestAllocator>::with_capacity_in(1_usize, VecTestAllocator { &state });
    values.push(7);
    state.fail = true;
    EXPECT_DEATH(values.insert(2_usize, 9), "Vec insertion index 2 exceeds length 1");
    EXPECT_DEATH((void)values.insert_mut(2_usize, 9), "Vec insertion index 2 exceeds length 1");
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

TEST(Vec, ClearAndTruncateDropOnlyRemovedNonTrivialElements) {
    int  drops  = 0;
    auto values = Vec<CollectMoveOnly>::make();
    for (int value = 0; value < 5; ++value) values.emplace_back(value, drops);

    values.truncate(usize(3));
    EXPECT_EQ(values.len(), usize(3));
    EXPECT_EQ(drops, 2);

    values.truncate(usize(4));
    EXPECT_EQ(values.len(), usize(3));
    EXPECT_EQ(drops, 2);

    values.clear();
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(drops, 5);

    values.clear();
    EXPECT_EQ(drops, 5);
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

TEST(Vec, StatefulAllocatorOwnsBufferAcrossMoveAndIteration) {
    VecAllocatorState first {};
    VecAllocatorState second {};

    {
        auto source =
            Vec<int, VecTestAllocator>::with_capacity_in(usize(2), VecTestAllocator { &first });
        source.push(1);
        source.push(2);
        source.push(3);

        auto destination =
            Vec<int, VecTestAllocator>::with_capacity_in(usize(1), VecTestAllocator { &second });
        destination.push(9);
        destination = rstd::move(source);

        EXPECT_EQ(destination.allocator().state, &first);
        auto iterator = rstd::move(destination).into_iter();
        EXPECT_EQ(iterator.next().unwrap(), 1);
    }

    EXPECT_EQ(first.allocations, first.deallocations);
    EXPECT_EQ(second.allocations, second.deallocations);
    EXPECT_GT(first.allocations, 1);
}

TEST(Vec, CloneInSelectsDestinationAllocator) {
    VecAllocatorState source_state {};
    VecAllocatorState clone_state {};

    {
        auto source = Vec<int, VecTestAllocator>::with_capacity_in(
            usize(2), VecTestAllocator { &source_state });
        source.push(4);
        source.push(7);

        auto clone = source.clone_in(VecTestAllocator { &clone_state });
        EXPECT_EQ(clone.allocator().state, &clone_state);
        ASSERT_EQ(clone.len(), usize(2));
        EXPECT_EQ(clone[usize()], 4);
        EXPECT_EQ(clone[usize(1)], 7);
    }

    EXPECT_EQ(source_state.allocations, source_state.deallocations);
    EXPECT_EQ(clone_state.allocations, clone_state.deallocations);
}

TEST(Vec, FromIterInUsesSelectedAllocator) {
    VecAllocatorState state {};
    {
        auto values = Vec<i32, VecTestAllocator>::from_iter_in(rstd::iter::range(0_i32, 5_i32),
                                                               VecTestAllocator { &state });
        EXPECT_EQ(values.allocator().state, &state);
        ASSERT_EQ(values.len(), usize(5));
        EXPECT_EQ(values[usize(4)], 4_i32);
    }
    EXPECT_EQ(state.allocations, state.deallocations);
}

TEST(Vec, AppendPreservesSourceAllocationAndAllocatorOwnership) {
    for (int spare = 0; spare <= 8; spare += 8) {
        VecAllocatorState left {}, right {};
        {
            auto target = Vec<int, VecTestAllocator>::with_capacity_in(usize(1 + spare),
                                                                       VecTestAllocator { &left });
            auto source =
                Vec<int, VecTestAllocator>::with_capacity_in(4_usize, VecTestAllocator { &right });
            target.push(1);
            source.push(2);
            source.push(3);
            auto* source_data     = source.data();
            auto  source_capacity = source.capacity();
            auto  allocations     = left.allocations;
            target.append(source);
            ASSERT_EQ(target.len(), 3_usize);
            EXPECT_EQ(target[0_usize], 1);
            EXPECT_EQ(target[1_usize], 2);
            EXPECT_EQ(target[2_usize], 3);
            EXPECT_TRUE(source.is_empty());
            EXPECT_EQ(source.data(), source_data);
            EXPECT_EQ(source.capacity(), source_capacity);
            EXPECT_EQ(right.allocations, 1);
            EXPECT_EQ(right.deallocations, 0);
            if (spare) EXPECT_EQ(left.allocations, allocations);
            source.push(4);
            EXPECT_EQ(source[0_usize], 4);
            target.append(source);
            EXPECT_EQ(target[3_usize], 4);
        }
        EXPECT_EQ(left.allocations, left.deallocations);
        EXPECT_EQ(right.allocations, right.deallocations);
    }
    Vec<u8> target, source;
    target.append(source);
    EXPECT_EQ(target.capacity(), 0_usize);
    source.push(u8(7));
    target.append(source);
    EXPECT_EQ(target[0_usize], u8(7));
    target[0_usize] = u8(9);
    EXPECT_EQ(target.data()[0], rstd::byte { 9 });
}

TEST(Vec, AppendMovesNonTrivialElementsWithoutAssignment) {
    for (int spare = 0; spare <= 8; spare += 8) {
        int drops = 0;
        {
            auto target = Vec<CollectMoveOnly>::with_capacity(usize(1 + spare));
            target.emplace_back(1, drops);
            {
                auto source = Vec<CollectMoveOnly>::with_capacity(2_usize);
                source.emplace_back(2, drops);
                source.emplace_back(3, drops);
                target.append(source);
                EXPECT_EQ(drops, 0);
                EXPECT_TRUE(source.is_empty());
            }
            EXPECT_EQ(drops, 0);
            for (usize i; i < target.len(); ++i) {
                EXPECT_EQ(target[i].value, int(i.to_primitive() + 1));
                EXPECT_EQ(target[i].value_ptr, &target[i].value);
            }
        }
        EXPECT_EQ(drops, 3);
    }
    int  drops  = 0;
    auto source = Vec<CollectMoveOnly>::with_capacity(1_usize);
    source.emplace_back(7, drops);
    {
        Vec<CollectMoveOnly> target;
        target.append(source);
    }
    EXPECT_EQ(drops, 1);
    EXPECT_TRUE(source.is_empty());
}

TEST(Vec, AppendRejectsSelfAndAllocatesBeforeMoving) {
    struct MoveProbe {
        MoveProbe() = default;
        MoveProbe(MoveProbe&&) { rstd::panic { "moved before reservation" }; }
    };
    VecAllocatorState state {};
    auto target = Vec<MoveProbe, VecTestAllocator>::new_in(VecTestAllocator { &state });
    auto source =
        Vec<MoveProbe, VecTestAllocator>::with_capacity_in(1_usize, VecTestAllocator { &state });
    source.emplace_back();
    state.fail = true;
    EXPECT_DEATH(source.append(source), "Vec cannot append itself");
    EXPECT_DEATH(target.append(source), "Vec allocation failed");
}

TEST(Vec, ResizeWithGeneratesOnlyNewElements) {
    int drops = 0;
    int calls = 0;
    {
        Vec<CollectMoveOnly> values;
        auto                 generate = [&] {
            return CollectMoveOnly(++calls, drops);
        };
        values.resize_with(3_usize, generate);
        EXPECT_EQ(calls, 3);
        values.resize_with(3_usize, generate);
        values.resize_with(1_usize, generate);
        EXPECT_EQ(calls, 3);
        EXPECT_EQ(drops, 2);
        values.resize_with(5_usize, generate);
        EXPECT_EQ(calls, 7);
        EXPECT_EQ(values[0_usize].value, 1);
        EXPECT_EQ(values[1_usize].value, 4);
        for (usize i; i < values.len(); ++i) EXPECT_EQ(values[i].value_ptr, &values[i].value);
    }
    EXPECT_EQ(drops, 7);
    Vec<u8> bytes;
    bytes.resize_with(5_usize, [next = u8(1)]() mutable {
        return next++;
    });
    EXPECT_EQ(bytes[4_usize], u8(5));
    auto     owner = Box<int>::make(12);
    Vec<int> values;
    values.resize_with(2_usize, [owner = rstd::move(owner)] {
        return *owner;
    });
    EXPECT_EQ(values[1_usize], 12);
}

TEST(Vec, ExtendFromWithinValidatesAndClonesOriginalRange) {
    for (int spare = 0; spare <= 8; spare += 8) {
        for (usize start; start <= 3_usize; ++start) {
            for (usize end = start; end <= 3_usize; ++end) {
                auto values = Vec<u8>::with_capacity(usize(3 + spare));
                values.push(u8(1));
                values.push(u8(2));
                values.push(u8(3));
                auto capacity = values.capacity();
                values.extend_from_within(start, end);
                EXPECT_EQ(values.len(), 3_usize + end - start);
                for (usize i; i < 3_usize; ++i) EXPECT_EQ(values[i], u8(i.to_primitive() + 1));
                for (usize i = start; i < end; ++i)
                    EXPECT_EQ(values[3_usize + i - start], u8(i.to_primitive() + 1));
                if (spare || start == end) EXPECT_EQ(values.capacity(), capacity);
            }
        }
    }
    int  clones = 0, clone_froms = 0;
    auto values = Vec<CloneTracked>::with_capacity(2_usize);
    values.emplace_back(3, clones, clone_froms);
    values.emplace_back(5, clones, clone_froms);
    values.extend_from_within(0_usize, 2_usize);
    EXPECT_EQ(clones, 2);
    EXPECT_EQ(values[2_usize].value, 3);
    EXPECT_EQ(values[3_usize].value, 5);
    VecAllocatorState state {};
    auto              invalid =
        Vec<int, VecTestAllocator>::with_capacity_in(1_usize, VecTestAllocator { &state });
    invalid.push(1);
    state.fail = true;
    EXPECT_DEATH(invalid.extend_from_within(1_usize, 0_usize), "Vec extend range out of bounds");
    EXPECT_DEATH(invalid.extend_from_within(0_usize, 2_usize), "Vec extend range out of bounds");
}

TEST(Vec, ExtendCopiedAndClonedSlicesSurvivesSelfGrowth) {
    using Extend = rstd::Impl<rstd::iter::Extend<u8>, Vec<u8>>;
    for (int mutable_source = 0; mutable_source < 2; ++mutable_source) {
        auto values = Vec<u8>::with_capacity(4_usize);
        for (int i = 0; i < 4; ++i) values.push(u8(i));
        if (mutable_source) {
            auto source = values.iter_mut().copied();
            (void)source.next();
            (void)source.next_back();
            Extend::extend(values, rstd::move(source));
        } else {
            auto source = values.iter().copied();
            (void)source.next();
            (void)source.next_back();
            Extend::extend(values, rstd::move(source));
        }
        ASSERT_EQ(values.len(), 6_usize);
        EXPECT_EQ(values[4_usize], u8(1));
        EXPECT_EQ(values[5_usize], u8(2));
    }
    int  clones = 0, clone_froms = 0;
    auto values = Vec<CloneTracked>::with_capacity(3_usize);
    for (int i = 0; i < 3; ++i) values.emplace_back(i, clones, clone_froms);
    auto source = values.iter().cloned();
    (void)source.next();
    rstd::Impl<rstd::iter::Extend<CloneTracked>, Vec<CloneTracked>>::extend(values,
                                                                            rstd::move(source));
    EXPECT_EQ(clones, 3);
    ASSERT_EQ(values.len(), 5_usize);
    EXPECT_EQ(values[3_usize].value, 1);
    EXPECT_EQ(values[4_usize].value, 2);
}

TEST(Vec, ExtendOwningIteratorRetiresOnlyRemainingElements) {
    VecAllocatorState source_state {}, target_state {};
    int               drops = 0;
    using V                 = Vec<CollectMoveOnly, VecTestAllocator>;
    {
        auto target = V::with_capacity_in(4_usize, VecTestAllocator { &target_state });
        target.emplace_back(9, drops);
        auto source = V::with_capacity_in(4_usize, VecTestAllocator { &source_state });
        for (int i = 0; i < 4; ++i) source.emplace_back(i, drops);
        {
            auto iterator = rstd::move(source).into_iter();
            (void)iterator.next();
            (void)iterator.next_back();
            EXPECT_EQ(drops, 2);
            rstd::Impl<rstd::iter::Extend<CollectMoveOnly>, V>::extend(target,
                                                                       rstd::move(iterator));
        }
        EXPECT_EQ(source_state.deallocations, 1);
        EXPECT_EQ(target_state.allocations, 1);
        EXPECT_EQ(drops, 2);
        ASSERT_EQ(target.len(), 3_usize);
        EXPECT_EQ(target[1_usize].value, 1);
        EXPECT_EQ(target[2_usize].value, 2);
        for (usize i; i < target.len(); ++i) EXPECT_EQ(target[i].value_ptr, &target[i].value);
    }
    EXPECT_EQ(drops, 5);
    EXPECT_EQ(target_state.allocations, target_state.deallocations);
    Vec<u8> bytes;
    auto    source = Vec<u8, VecTestAllocator>::new_in(VecTestAllocator { &source_state });
    source.push(u8(42));
    rstd::Impl<rstd::iter::Extend<u8>, Vec<u8>>::extend(bytes, rstd::move(source).into_iter());
    EXPECT_EQ(bytes[0_usize], u8(42));
    Vec<u8> empty;
    rstd::Impl<rstd::iter::Extend<u8>, Vec<u8>>::extend(bytes, rstd::move(empty).into_iter());
    EXPECT_EQ(bytes.len(), 1_usize);
}

TEST(Vec, ExtendTrustedPreservesCallbacksAndReservesOnce) {
    VecAllocatorState state {};
    using V     = Vec<u8, VecTestAllocator>;
    auto values = V::new_in(VecTestAllocator { &state });
    int  calls  = 0;
    auto source = rstd::iter::range(0_usize, 64_usize).map([&](usize i) {
        ++calls;
        return u8(i.to_primitive());
    });
    static_assert(rstd::Impled<decltype(source), rstd::iter::TrustedLen>);
    rstd::Impl<rstd::iter::Extend<u8>, V>::extend(values, rstd::move(source));
    EXPECT_EQ(calls, 64);
    EXPECT_EQ(state.allocations, 1);
    EXPECT_EQ(values.len(), 64_usize);
    EXPECT_EQ(values[63_usize], u8(63));
    auto empty = rstd::iter::range(0_usize, 0_usize).map([&](usize) {
        ++calls;
        return u8(0);
    });
    rstd::Impl<rstd::iter::Extend<u8>, V>::extend(values, rstd::move(empty));
    EXPECT_EQ(calls, 64);
    EXPECT_EQ(state.allocations, 1);
}

TEST(Vec, ExtendOrdinaryIteratorPreservesUnknownHintsAndSideEffects) {
    int      hints = 0;
    Vec<int> values;
    rstd::Impl<rstd::iter::Extend<int>, Vec<int>>::extend(values,
                                                          HintedCollectIterator(100, hints, false));
    ASSERT_EQ(values.len(), 100_usize);
    EXPECT_GT(hints, 1);
    for (usize i; i < values.len(); ++i) EXPECT_EQ(values[i], int(i.to_primitive()));
    int      calls  = 0;
    auto     source = rstd::iter::range(0_i32, 6_i32).filter([&](i32 value) {
        ++calls;
        return value % i32(2) == i32(0);
    });
    Vec<i32> filtered;
    rstd::Impl<rstd::iter::Extend<i32>, Vec<i32>>::extend(filtered, rstd::move(source));
    EXPECT_EQ(calls, 6);
    EXPECT_EQ(filtered.len(), 3_usize);
    EXPECT_EQ(filtered[2_usize], i32(4));
}

struct OverflowExtendIterator : rstd::DefaultInClass<OverflowExtendIterator, rstd::iter::Iterator> {
    using Item                               = int;
    static constexpr bool PROVEN_TRUSTED_LEN = true;
    auto next() -> Option<int> { rstd::panic { "consumed before length validation" }; }
    auto size_hint() const -> rstd::iter::SizeHint { return { usize::MAX, rstd::None() }; }
};

TEST(Vec, ExtendRejectsUnrepresentableTrustedLengthBeforeConsumption) {
    Vec<int> values;
    EXPECT_DEATH(
        (rstd::Impl<rstd::iter::Extend<int>, Vec<int>>::extend(values, OverflowExtendIterator {})),
        "Vec capacity overflow");
}

TEST(Vec, ExtendGeneratedMoveOnlyValuesTracksInitialization) {
    int drops = 0;
    int calls = 0;
    {
        Vec<CollectMoveOnly> values;
        auto                 source = rstd::iter::repeat_with([&] {
                          return CollectMoveOnly(++calls, drops);
                                      }).take(4_usize);
        rstd::Impl<rstd::iter::Extend<CollectMoveOnly>, Vec<CollectMoveOnly>>::extend(
            values, rstd::move(source));
        EXPECT_EQ(calls, 4);
        EXPECT_EQ(drops, 0);
        auto filtered = rstd::iter::repeat_with([&] {
                            return CollectMoveOnly(++calls, drops);
                        })
                            .take(4_usize)
                            .filter([](const CollectMoveOnly& value) {
                                return value.value % 2 == 0;
                            });
        rstd::Impl<rstd::iter::Extend<CollectMoveOnly>, Vec<CollectMoveOnly>>::extend(
            values, rstd::move(filtered));
        EXPECT_EQ(calls, 8);
        EXPECT_EQ(drops, 2);
        EXPECT_EQ(values.len(), 6_usize);
        for (usize i; i < values.len(); ++i) EXPECT_EQ(values[i].value_ptr, &values[i].value);
        EXPECT_EQ(values[4_usize].value, 6);
        EXPECT_EQ(values[5_usize].value, 8);
    }
    EXPECT_EQ(drops, 8);
}

TEST(Vec, CollectTrustedPipelinePreservesAllocatorAndMoveOnlyLifetime) {
    VecAllocatorState state {};
    int               drops = 0;
    int               calls = 0;
    {
        const i32 data[] = { 1_i32, 2_i32, 3_i32, 4_i32 };
        auto      source = rstd::iter::from_array(data)
                               .copied()
                               .map([&](i32 value) {
                              ++calls;
                              return CollectMoveOnly(value.to_primitive(), drops);
                               })
                               .take(3_usize);
        auto      values = Vec<CollectMoveOnly, VecTestAllocator>::from_iter_in(
            rstd::move(source), VecTestAllocator { &state });
        EXPECT_EQ(state.allocations, 1);
        EXPECT_EQ(calls, 3);
        EXPECT_EQ(drops, 0);
        ASSERT_EQ(values.len(), 3_usize);
        for (usize i; i < values.len(); ++i) {
            EXPECT_EQ(values[i].value, int(i.to_primitive()) + 1);
            EXPECT_EQ(values[i].value_ptr, &values[i].value);
        }
    }
    EXPECT_EQ(drops, 3);
    EXPECT_EQ(state.allocations, state.deallocations);
}

TEST(Vec, CollectTrustedClonePipelineKeepsCloneCount) {
    int          clones      = 0;
    int          clone_froms = 0;
    CloneTracked data[]      = { { 1, clones, clone_froms },
                                 { 2, clones, clone_froms },
                                 { 3, clones, clone_froms } };
    auto         values      = rstd::iter::from_array(data)
                                   .cloned()
                                   .map([](CloneTracked value) {
                          value.value *= 2;
                          return value;
                                   })
                                   .collect<Vec<CloneTracked>>();
    EXPECT_EQ(clones, 3);
    EXPECT_EQ(clone_froms, 0);
    ASSERT_EQ(values.len(), 3_usize);
    EXPECT_EQ(values[0_usize].value, 2);
    EXPECT_EQ(values[2_usize].value, 6);
}

struct ByteConversionPadded {
    char tag;
    int  value;
};
struct ByteConversionCopyable {
    u32 value;
};
template<typename T>
concept CanOwnBytes = requires(Vec<T>&& values) { rstd::move(values).into_bytes(); };
static_assert(CanOwnBytes<float> && CanOwnBytes<double> && CanOwnBytes<u32> && CanOwnBytes<f32>);
static_assert(CanOwnBytes<byte> && CanOwnBytes<u8> && CanOwnBytes<int>);
static_assert(! CanOwnBytes<bool> && ! CanOwnBytes<int*> && ! CanOwnBytes<long double>);
static_assert(! CanOwnBytes<ByteConversionPadded> && ! CanOwnBytes<ByteConversionCopyable>);
static_assert(! CanOwnBytes<Box<int>> && ! CanOwnBytes<rstd::mem::MaybeUninit<u32>>);

TEST(Vec, IntoBytesTransfersNativeRepresentationWithoutAllocation) {
    auto values = Vec<u32>::with_capacity(usize(8));
    values.push(u32(0x12345678));
    values.push(u32(0xabcdef01));
    u32   expected[] { u32(0x12345678), u32(0xabcdef01) };
    auto* pointer = reinterpret_cast<byte*>(values.data());
    auto  bytes   = rstd::move(values).into_bytes();
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(values.capacity(), usize());
    EXPECT_EQ(bytes.data(), pointer);
    EXPECT_EQ(bytes.len(), usize(sizeof(expected)));
    EXPECT_EQ(bytes.capacity(), usize(8 * sizeof(u32)));
    EXPECT_EQ(bytes.allocation_alignment(), usize(alignof(u32)));
    EXPECT_EQ(__builtin_memcmp(bytes.data(), expected, sizeof(expected)), 0);
    bytes.reserve(usize(256));
    EXPECT_EQ(bytes.allocation_alignment(), usize(alignof(u32)));
    bytes.shrink_to_fit();
    EXPECT_EQ(bytes.allocation_alignment(), usize(alignof(u32)));
    EXPECT_EQ(__builtin_memcmp(bytes.data(), expected, sizeof(expected)), 0);
}

TEST(Vec, AlignedBytesRetainAlignmentAcrossZeroCapacity) {
    auto bytes = Vec<u8>::with_capacity_aligned_in(usize(32), usize(256), ::alloc::Global {});
    bytes.push(u8(7));
    bytes.shrink_to_fit();
    EXPECT_EQ(bytes.allocation_alignment(), usize(256));
    EXPECT_EQ(bytes.capacity(), usize(1));
    bytes.clear();
    bytes.shrink_to_fit();
    EXPECT_EQ(bytes.capacity(), usize());
    EXPECT_EQ(bytes.allocation_alignment(), usize(256));
    bytes.push(u8(9));
    EXPECT_EQ(bytes.allocation_alignment(), usize(256));
    EXPECT_EQ(reinterpret_cast<rstd::uintptr_t>(bytes.data()) % 256, 0u);
}

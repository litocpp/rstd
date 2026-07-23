#include <gtest/gtest.h>
#include <algorithm>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

import rstd.cppstd;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace iter = rstd::iter;

namespace
{

struct MoveOnly {
    int value;

    explicit MoveOnly(int value): value(value) {}
    MoveOnly(const MoveOnly&)                    = delete;
    auto operator=(const MoveOnly&) -> MoveOnly& = delete;
    MoveOnly(MoveOnly&&)                         = default;
    auto operator=(MoveOnly&&) -> MoveOnly&      = default;
};

struct HintedIterator : rstd::DefaultInClass<HintedIterator, iter::Iterator> {
    using Item = int;

    auto next() -> rstd::Option<int> {
        if (next_value == 33) return rstd::None();
        return rstd::Some(next_value++);
    }

    auto size_hint() const -> iter::SizeHint {
        auto remaining = rstd::usize(static_cast<rstd::size_t>(33 - next_value));
        return { remaining, rstd::Some(remaining) };
    }

    int next_value {};
};

template<class T>
struct TrackingAllocator {
    using value_type = T;

    TrackingAllocator() = default;
    template<class U>
    TrackingAllocator(const TrackingAllocator<U>&) {}

    auto allocate(std::size_t size) -> T* {
        if (first_allocation == 0) first_allocation = size;
        return std::allocator<T> {}.allocate(size);
    }

    void deallocate(T* pointer, std::size_t size) {
        std::allocator<T> {}.deallocate(pointer, size);
    }

    static inline std::size_t first_allocation {};
};

template<class T, class U>
constexpr auto operator==(const TrackingAllocator<T>&, const TrackingAllocator<U>&) -> bool {
    return true;
}

template<typename R>
concept CanFromRange = requires(R&& range) { iter::from_range(std::forward<R>(range)); };

} // namespace

TEST(CppStdIter, IteratorRangeModelsSinglePassInputRange) {
    auto range = iter::as_range(iter::range(0_i32, 5_i32));
    static_assert(std::ranges::input_range<decltype(range)>);
    static_assert(std::ranges::view<decltype(range)>);
    static_assert(std::ranges::viewable_range<decltype(range)>);
    static_assert(! std::ranges::forward_range<decltype(range)>);
    static_assert(! std::ranges::random_access_range<decltype(range)>);
    static_assert(! std::ranges::contiguous_range<decltype(range)>);

    auto sum = i32();
    for (auto value : range) sum += value;
    EXPECT_EQ(sum, 10_i32);
}

TEST(CppStdIter, IteratorRangeSupportsRangesAlgorithmsAndMoveOnlyValues) {
    auto range = iter::as_range(iter::range(1_i32, 6_i32));
    EXPECT_EQ(std::ranges::max(range), 5_i32);

    auto found_range = iter::as_range(iter::range(1_i32, 6_i32));
    auto found       = std::ranges::find(found_range, 4_i32);
    ASSERT_NE(found, found_range.end());
    EXPECT_EQ(*found, 4_i32);

    auto visited_range = iter::as_range(iter::range(1_i32, 4_i32));
    auto sum           = i32();
    std::ranges::for_each(visited_range, [&](i32 value) { sum += value; });
    EXPECT_EQ(sum, 6_i32);

    auto move_range = iter::as_range(iter::once(MoveOnly(7)));
    auto cursor     = move_range.begin();
    auto value      = std::ranges::iter_move(cursor);
    EXPECT_EQ(value.value, 7);
}

TEST(CppStdIter, IteratorRangePreservesRstdBorrowItems) {
    auto values = rstd::vec::Vec<i32> {};
    values.push(1_i32);
    values.push(2_i32);
    values.push(3_i32);

    auto borrowed = iter::as_range(values.iter());
    static_assert(std::ranges::input_range<decltype(borrowed)>);
    auto found = std::ranges::find_if(borrowed, [](rstd::ref<i32> value) {
        return *value == 2_i32;
    });
    ASSERT_NE(found, borrowed.end());
    EXPECT_EQ(**found, 2_i32);

    auto mutable_borrowed = iter::as_range(values.iter_mut());
    std::ranges::for_each(mutable_borrowed, [](rstd::mut_ref<i32> value) {
        *value += 10_i32;
    });
    EXPECT_EQ(values[0_usize], 11_i32);
    EXPECT_EQ(values[1_usize], 12_i32);
    EXPECT_EQ(values[2_usize], 13_i32);
}

TEST(CppStdIter, FromRangePreservesReferenceAndCapabilities) {
    auto values = std::vector<int> { 2, 3, 5 };
    auto range  = iter::from_range(values);
    static_assert(rstd::mtp::same_as<typename decltype(range)::Item, int&>);
    static_assert(rstd::Impled<decltype(range), iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<decltype(range), iter::ExactSizeIterator>);

    auto first = range.next();
    ASSERT_TRUE(first.is_some());
    *first = 7;
    EXPECT_EQ(values[0], 7);
    EXPECT_EQ(range.next_back(), rstd::Some<int&>(values[2]));
    EXPECT_EQ(range.len(), 1_usize);

    auto const& immutable = values;
    auto const_range      = iter::from_range(immutable);
    static_assert(rstd::mtp::same_as<typename decltype(const_range)::Item, const int&>);
    EXPECT_EQ(*const_range.next(), 7);

    auto copied = iter::from_range(values).copied();
    static_assert(rstd::mtp::same_as<typename decltype(copied)::Item, int>);
    static_assert(rstd::Impled<decltype(copied), iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<decltype(copied), iter::ExactSizeIterator>);
    EXPECT_EQ(copied.next(), rstd::Some(7));
    EXPECT_EQ(copied.next_back(), rstd::Some(5));
}

TEST(CppStdIter, FromRangeAcceptsBorrowedRvaluesAndRejectsOwningRvalues) {
    auto values = std::array<int, 3> { 1, 4, 9 };
    auto view   = std::span<int>(values);
    auto range  = iter::from_range(view);
    EXPECT_EQ(*range.next(), 1);

    static_assert(CanFromRange<std::span<int>>);
    static_assert(! CanFromRange<std::vector<int>>);
}

TEST(CppStdIter, CollectsStandardSequenceContainersInInputOrder) {
    auto vector = iter::range(0_i32, 4_i32)
                      .map([](i32 value) { return value.to_primitive(); })
                      .collect<std::vector<int>>();
    EXPECT_EQ(vector, (std::vector<int> { 0, 1, 2, 3 }));

    auto deque = iter::range(0_i32, 3_i32)
                     .map([](i32 value) { return value.to_primitive(); })
                     .collect<std::deque<int>>();
    EXPECT_EQ(deque, (std::deque<int> { 0, 1, 2 }));

    auto list = iter::range(0_i32, 3_i32)
                    .map([](i32 value) { return value.to_primitive(); })
                    .collect<std::list<int>>();
    EXPECT_EQ(list, (std::list<int> { 0, 1, 2 }));

    auto forward = iter::range(0_i32, 3_i32)
                       .map([](i32 value) { return value.to_primitive(); })
                       .collect<std::forward_list<int>>();
    EXPECT_EQ(forward, (std::forward_list<int> { 0, 1, 2 }));

    auto string = iter::range(0_i32, 3_i32)
                      .map([](i32 value) { return static_cast<char>('a' + value.to_primitive()); })
                      .collect<std::string>();
    EXPECT_EQ(string, "abc");
}

TEST(CppStdIter, CollectsStandardAssociativeContainersWithDuplicateSemantics) {
    auto values = [] {
        return iter::range(0_i32, 4_i32).map([](i32 value) {
            return value.to_primitive() % 2;
        });
    };

    auto set = values().collect<std::set<int>>();
    EXPECT_EQ(set, (std::set<int> { 0, 1 }));
    auto multiset = values().collect<std::multiset<int>>();
    EXPECT_EQ(multiset.count(0), 2);

    auto unordered = values().collect<std::unordered_set<int>>();
    EXPECT_EQ(unordered.size(), 2);
    auto unordered_multi = values().collect<std::unordered_multiset<int>>();
    EXPECT_EQ(unordered_multi.count(1), 2);

    auto pairs = [] {
        return iter::range(0_i32, 4_i32).map([](i32 value) {
            return rstd::tuple<int, int>(value.to_primitive() % 2, value.to_primitive());
        });
    };

    auto map = pairs().collect<std::map<int, int>>();
    EXPECT_EQ(map.size(), 2);
    EXPECT_EQ(map.at(0), 0);
    auto multimap = pairs().collect<std::multimap<int, int>>();
    EXPECT_EQ(multimap.count(0), 2);

    auto unordered_map = pairs().collect<std::unordered_map<int, int>>();
    EXPECT_EQ(unordered_map.size(), 2);
    auto unordered_multimap = pairs().collect<std::unordered_multimap<int, int>>();
    EXPECT_EQ(unordered_multimap.count(1), 2);
}

TEST(CppStdIter, CollectsMoveOnlyItemsAndUsesLowerSizeHintForReserve) {
    auto values = iter::once(MoveOnly(7)).collect<std::vector<MoveOnly>>();
    ASSERT_EQ(values.size(), 1);
    EXPECT_EQ(values[0].value, 7);

    TrackingAllocator<int>::first_allocation = 0;
    auto hinted = HintedIterator().collect<std::vector<int, TrackingAllocator<int>>>();
    EXPECT_EQ(hinted.size(), 33);
    EXPECT_EQ(TrackingAllocator<int>::first_allocation, 33);
}

TEST(CppStdIter, CollectsStandardContainerAdaptersWithTheirNativeOrder) {
    auto source = [] {
        return iter::range(0_i32, 4_i32).map([](i32 value) { return value.to_primitive(); });
    };

    auto queue = source().collect<std::queue<int>>();
    EXPECT_EQ(queue.front(), 0);
    EXPECT_EQ(queue.back(), 3);

    auto stack = source().collect<std::stack<int>>();
    EXPECT_EQ(stack.top(), 3);

    auto priority = source().collect<std::priority_queue<int>>();
    EXPECT_EQ(priority.top(), 3);
}

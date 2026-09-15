#include <rstd/test/gtest.hpp>

import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace iter = rstd::iter;

template<typename T>
struct ExpectedItem {
    T value;

    template<typename U>
    friend constexpr auto operator==(const Option<U>& item, const ExpectedItem& expected) -> bool {
        return item.is_some() && *item == expected.value;
    }
};

constexpr auto slice_queries() -> bool {
    const int values[] = { 1, 2, 3, 4 };
    auto      source   = iter::from_slice(slice<int>::from_raw_parts(values, 4_usize));
    if (! source.any([](auto value) {
            return *value == 2;
        }))
        return false;
    auto found = source.next();
    if (found.is_none() || found->as_raw_ptr() != &values[2]) return false;
    if (source.all([](auto value) {
            return *value < 4;
        }))
        return false;
    if (source.next().is_some()) return false;
    auto empty = iter::from_slice(slice<int>::from_raw_parts(nullptr, usize {}));
    return empty.is_empty() && empty.all([](auto) {
        return false;
    }) && ! empty.any([](auto) {
        return true;
    });
}

constexpr auto mutable_slice() -> bool {
    int values[] = { 1, 2, 3 };
    iter::from_array_mut(values).for_each([](auto value) {
        *value += 10;
    });
    auto source = iter::from_array(values);
    auto trait  = rstd::as<iter::Iterator>(source);
    auto size   = rstd::as<iter::ExactSizeIterator>(source);
    if (size.len() != 3_usize || trait.size_hint().template get<0>() != 3_usize) return false;
    auto item = trait.next();
    return item.is_some() && **item == 11 && values[1] == 12 && values[2] == 13;
}

constexpr auto adapter_next() -> bool {
    const int values[] = { 1, 2, 3, 4 };
    auto      source   = iter::from_array(values)
                             .map([](auto value) {
                          return *value * 2;
                             })
                             .filter([](int value) {
                          return value >= 4;
                             })
                             .enumerate()
                             .zip(iter::range(10_i32, 13_i32));
    auto      item     = source.next();
    if (item.is_none()) return false;
    const auto& indexed = item->template get<0>();
    if (indexed.template get<0>() != 0_usize || indexed.template get<1>() != 4 ||
        item->template get<1>() != 10_i32)
        return false;
    return rstd::move(source).count() == 2_usize;
}

constexpr auto driver_short_circuit() -> bool {
    int  visited = 0;
    auto source  = iter::range(1_i32, 3_i32)
                       .chain(iter::range(3_i32, 5_i32))
                       .inspect([&](i32) {
                          ++visited;
                       })
                       .map([](i32 value) {
                          return value * 2_i32;
                       })
                       .filter([](i32 value) {
                          return value >= 4_i32;
                       });
    if (! source.by_ref().any([](i32 value) {
            return value == 6_i32;
        }) ||
        visited != 3)
        return false;
    if (source.next() != ExpectedItem(8_i32) || visited != 4) return false;
    return source.next().is_none();
}

constexpr auto traversal() -> bool {
    auto range = iter::range(-3_i32, 4_i32);
    if (range.nth(3_usize) != ExpectedItem(0_i32)) return false;
    if (range.nth_back(1_usize) != ExpectedItem(2_i32)) return false;
    if (rstd::move(range).last() != ExpectedItem(1_i32)) return false;
    auto short_range = iter::range(0_i32, 2_i32);
    auto advanced    = short_range.advance_by(4_usize);
    if (advanced.is_ok() || advanced.unwrap_err().get() != 2_usize || short_range.next().is_some())
        return false;
    const int values[] = { 1, 2, 3, 4 };
    auto      source   = iter::from_array(values);
    if (source.advance_by(1_usize).is_err()) return false;
    if (source.by_ref().take(1_usize).count() != 1_usize) return false;
    auto last = rstd::move(source).last();
    return last.is_some() && last->as_raw_ptr() == &values[3] &&
           iter::range(-3_i32, 4_i32).count() == 7_usize;
}

constexpr auto generators_and_selection() -> bool {
    if (iter::empty<int>().count() != 0_usize) return false;
    if (iter::once(3).next_back() != ExpectedItem(3)) return false;
    int  calls = 0;
    auto once  = iter::once_with([&] {
        return ++calls;
    });
    if (once.next() != ExpectedItem(1) || once.next().is_some() || calls != 1) return false;
    if (iter::repeat(2).take(3_usize).sum() != 6) return false;
    if (iter::repeat_with([&] {
            return ++calls;
        })
            .take(2_usize)
            .sum() != 5)
        return false;
    auto successors = iter::successors(Some(1), [](int value) -> Option<int> {
        if (value < 3) return Some(value + 1);
        return None();
    });
    if (rstd::move(successors).sum() != 6) return false;
    if (iter::range(1_i32, 4_i32).cycle().take(5_usize).sum() != 9_i32) return false;
    if (iter::range(1_i32, 4_i32).intersperse(10_i32).sum() != 26_i32) return false;
    return iter::range(0_i32, 8_i32).skip(1_usize).take(5_usize).step_by(2_usize).sum() == 9_i32;
}

constexpr auto stateful_adapters() -> bool {
    auto peeked = iter::range(1_i32, 4_i32).peekable();
    if (*peeked.peek() != 1_i32 || peeked.len() != 3_usize) return false;
    *peeked.peek_mut() = 9_i32;
    if (peeked.next_if_eq(1_i32).is_some() || peeked.next_if_eq(9_i32) != ExpectedItem(9_i32))
        return false;
    if (peeked.next_back() != ExpectedItem(3_i32) || peeked.next() != ExpectedItem(2_i32))
        return false;
    if (peeked.peek() != nullptr) return false;
    int  calls = 0;
    auto fused = iter::from_fn([&]() -> Option<int> {
                     ++calls;
                     if (calls == 2) return None();
                     return Some(calls);
                 }).fuse();
    if (fused.next() != ExpectedItem(1) || fused.next().is_some() || fused.next().is_some() ||
        calls != 2)
        return false;
    auto scanned = iter::range(1_i32, 4_i32).scan(0_i32, [](i32& total, i32 value) {
        total += value;
        return Some(total);
    });
    return rstd::move(scanned).sum() == 10_i32;
}

constexpr auto filtering_and_flattening() -> bool {
    auto mapped = iter::range(0_i32, 6_i32).filter_map([](i32 value) -> Option<i32> {
        if (value % 2_i32 == 0_i32) return Some(value);
        return None();
    });
    if (rstd::move(mapped).sum() != 6_i32) return false;
    auto prefix = iter::range(1_i32, 5_i32).map_while([](i32 value) -> Option<i32> {
        if (value == 3_i32) return None();
        return Some(value);
    });
    if (rstd::move(prefix).sum() != 3_i32) return false;
    auto selected = iter::range(0_i32, 6_i32)
                        .skip_while([](i32 value) {
                            return value < 2_i32;
                        })
                        .take_while([](i32 value) {
                            return value < 4_i32;
                        });
    if (rstd::move(selected).sum() != 5_i32) return false;
    auto flat = iter::range(1_i32, 4_i32)
                    .map([](i32 value) {
                        return iter::once(value);
                    })
                    .flatten();
    if (flat.next() != ExpectedItem(1_i32) || flat.next_back() != ExpectedItem(3_i32) ||
        flat.next() != ExpectedItem(2_i32) || flat.next_back().is_some())
        return false;
    return iter::range(1_i32, 4_i32)
               .flat_map([](i32 value) {
                   return iter::range(0_i32, value);
               })
               .sum() == 4_i32;
}

constexpr auto comparisons_and_search() -> bool {
    const int values[] = { 1, 2, 3 };
    if (iter::from_array(values).cloned().sum() != 6 ||
        iter::from_array(values).copied().product() != 6)
        return false;
    if (iter::from_array(values).position([](auto value) {
            return *value == 2;
        }) != ExpectedItem(1_usize))
        return false;
    if (iter::range(1_i32, 4_i32).rposition([](i32 value) {
            return value < 3_i32;
        }) != ExpectedItem(1_usize))
        return false;
    if (iter::range(1_i32, 4_i32).find([](i32 value) {
            return value == 2_i32;
        }) != ExpectedItem(2_i32))
        return false;
    if (iter::range(1_i32, 4_i32).rfind([](i32 value) {
            return value < 3_i32;
        }) != ExpectedItem(2_i32))
        return false;
    if (! iter::range(1_i32, 4_i32).eq(iter::range(1_i32, 4_i32))) return false;
    if (iter::range(1_i32, 4_i32).cmp(iter::range(1_i32, 3_i32)) <= 0) return false;
    if (! iter::range(1_i32, 4_i32).is_sorted()) return false;
    return iter::range(1_i32, 4_i32).min() == ExpectedItem(1_i32) &&
           iter::range(1_i32, 4_i32).max() == ExpectedItem(3_i32) &&
           iter::range(1_i32, 4_i32).reduce([](i32 a, i32 b) {
               return a + b;
           }) == ExpectedItem(6_i32);
}

constexpr auto fallible_folds() -> bool {
    auto source = iter::range(1_i32, 6_i32).map([](i32 value) {
        return value * 2_i32;
    });
    auto folded = source.try_fold(0_i32, [](i32 total, i32 value) -> Result<i32, i32> {
        if (value == 6_i32) return Err(total);
        return Ok(total + value);
    });
    if (folded.is_ok() || folded.unwrap_err() != 6_i32 || source.next() != ExpectedItem(8_i32))
        return false;
    auto backwards = iter::range(1_i32, 5_i32).map([](i32 value) {
        return value * 2_i32;
    });
    auto reverse   = backwards.try_rfold(0_i32, [](i32 total, i32 value) -> Option<i32> {
        if (value == 6_i32) return None();
        return Some(total + value);
    });
    if (reverse.is_some() || backwards.next_back() != ExpectedItem(4_i32)) return false;
    if (iter::range(1_i32, 4_i32).rev().fold(0_i32, [](i32 a, i32 b) {
            return a * 10_i32 + b;
        }) != 321_i32)
        return false;
    return iter::range(1_i32, 4_i32).rfold(0_i32, [](i32 a, i32 b) {
        return a * 10_i32 + b;
    }) == 321_i32;
}

struct ConstexprValues {
    int          values[8] {};
    rstd::size_t length {};
};

template<>
struct rstd::Impl<iter::FromIterator<int>, ConstexprValues> {
    template<iter::has_next I>
    static constexpr auto from_iter(I source) -> ConstexprValues {
        ConstexprValues result;
        for (auto value = source.next(); value.is_some(); value = source.next())
            result.values[result.length++] = *value;
        return result;
    }
};

constexpr auto fallible_collection() -> bool {
    auto source = iter::range(0_i32, 4_i32).map([](i32 value) -> Result<int, int> {
        if (value == 2_i32) return Err(17);
        return Ok(value.to_primitive());
    });
    auto result = source.by_ref().collect<Result<ConstexprValues, int>>();
    if (result.is_ok() || result.unwrap_err() != 17) return false;
    auto next = source.next();
    if (next.is_none() || next->is_err() || **next != 3) return false;
    auto collected = iter::once(4).chain(iter::once(7)).collect<ConstexprValues>();
    if (collected.length != 2 || collected.values[0] != 4 || collected.values[1] != 7) return false;
    auto success = iter::once(Result<int, int>(Ok(5))).collect<Result<ConstexprValues, int>>();
    if (success.is_err() || success->length != 1 || success->values[0] != 5) return false;
    auto missing = iter::once(Option<int>(None())).collect<Option<ConstexprValues>>();
    if (missing.is_some()) return false;
    auto sum = iter::once(Result<int, int>(Ok(3)))
                   .chain(iter::once(Result<int, int>(Err(9))))
                   .sum<Result<int, int>>();
    return sum.is_err() && sum.unwrap_err() == 9 &&
           iter::once(Option<int>(Some(3))).product<Option<int>>() == ExpectedItem(3);
}

constexpr auto range_for_consumption() -> bool {
    int  values[] = { 1, 2, 3 };
    auto source   = iter::from_array_mut(values);
    for (auto value : iter::for_range(source)) {
        *value += 10;
        if (*value == 12) break;
    }
    auto remaining = source.next();
    if (values[0] != 11 || values[1] != 12 || values[2] != 3 || remaining.is_none() ||
        remaining->as_raw_ptr() != &values[2])
        return false;
    if (source.next().is_some()) return false;
    i32 sum {};
    for (auto value : iter::range(1_i32, 4_i32).rev()) sum += value;
    return sum == 6_i32;
}

static_assert(range_for_consumption());
static_assert(slice_queries());
static_assert(mutable_slice());
static_assert(adapter_next());
static_assert(driver_short_circuit());
static_assert(traversal());
static_assert(generators_and_selection());
static_assert(stateful_adapters());
static_assert(filtering_and_flattening());
static_assert(comparisons_and_search());
static_assert(fallible_folds());
static_assert(fallible_collection());

TEST(Iter, ConstexprOperationsAlsoRunAtRuntime) {
    EXPECT_TRUE(range_for_consumption());
    EXPECT_TRUE(slice_queries());
    EXPECT_TRUE(mutable_slice());
    EXPECT_TRUE(adapter_next());
    EXPECT_TRUE(driver_short_circuit());
    EXPECT_TRUE(traversal());
    EXPECT_TRUE(generators_and_selection());
    EXPECT_TRUE(stateful_adapters());
    EXPECT_TRUE(filtering_and_flattening());
    EXPECT_TRUE(comparisons_and_search());
    EXPECT_TRUE(fallible_folds());
    EXPECT_TRUE(fallible_collection());
}

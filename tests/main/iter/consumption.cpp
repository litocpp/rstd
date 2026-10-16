#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace iter = rstd::iter;

template<bool Reverse, typename I>
auto consumption_next(I& source) {
    if constexpr (Reverse)
        return rstd::as<iter::DoubleEndedIterator>(source).next_back();
    else
        return source.next();
}

template<bool Reverse, typename Factory>
void compare_consumption(Factory make) {
    for (int pre = 0; pre < 3; ++pre) {
        for (int stop = -1; stop < 8; ++stop) {
            Vec<i32> actual_log;
            Vec<i32> expected_log;
            auto     actual   = make(actual_log);
            auto     expected = make(expected_log);
            for (int i = 0; i < pre; ++i)
                EXPECT_EQ(consumption_next<! Reverse>(actual),
                          consumption_next<! Reverse>(expected));
            int  calls = 0;
            auto step  = [&](i32 sum, i32 value) -> Option<i32> {
                actual_log.push(value + 1000_i32);
                if (calls++ == stop) return None();
                return Some(sum + value);
            };
            auto result = [&] {
                if constexpr (Reverse)
                    return actual.try_rfold(0_i32, step);
                else
                    return actual.try_fold(0_i32, step);
            }();
            auto reference = Some(0_i32);
            calls          = 0;
            for (auto item = consumption_next<Reverse>(expected); item.is_some();
                 item      = consumption_next<Reverse>(expected)) {
                expected_log.push(*item + 1000_i32);
                if (calls++ == stop) {
                    reference = None();
                    break;
                }
                reference = Some(*reference + *item);
            }
            EXPECT_EQ(result, reference);
            EXPECT_EQ(actual.size_hint(), expected.size_hint());
            EXPECT_EQ(actual_log.as_slice(), expected_log.as_slice());
            for (int i = 0; i < 12; ++i) {
                if (i % 2)
                    EXPECT_EQ(actual.next(), expected.next());
                else
                    EXPECT_EQ(consumption_next<true>(actual), consumption_next<true>(expected));
            }
            EXPECT_EQ(actual_log.as_slice(), expected_log.as_slice());
        }
        Vec<i32> actual_log;
        Vec<i32> expected_log;
        auto     actual   = make(actual_log);
        auto     expected = make(expected_log);
        auto     add      = [&](i32 sum, i32 value) {
            actual_log.push(value + 1000_i32);
            return sum + value;
        };
        auto result = [&] {
            if constexpr (Reverse)
                return actual.by_ref().rfold(0_i32, add);
            else
                return actual.by_ref().fold(0_i32, add);
        }();
        i32 sum;
        for (auto item = consumption_next<Reverse>(expected); item.is_some();
             item      = consumption_next<Reverse>(expected)) {
            expected_log.push(*item + 1000_i32);
            sum += *item;
        }
        EXPECT_EQ(result, sum);
        EXPECT_EQ(actual_log.as_slice(), expected_log.as_slice());
        EXPECT_EQ(actual.next(), expected.next());
        EXPECT_EQ(consumption_next<true>(actual), consumption_next<true>(expected));
    }
}

template<typename Factory>
void compare_both_directions(Factory make) {
    compare_consumption<false>(make);
    compare_consumption<true>(make);
}

TEST(IterConsumption, AdaptersMatchPullStateAndCallbackOrder) {
    const i32 data[] = { 0_i32, 1_i32, 2_i32, 3_i32, 4_i32, 5_i32 };
    auto      source = [&](Vec<i32>& log) {
        return iter::from_array(data).copied().inspect([&log](i32 value) {
            log.push(i32(value));
        });
    };
    compare_both_directions(source);
    compare_both_directions([&](auto& log) {
        return source(log).filter([](i32 value) {
            return value % 2_i32 == 0_i32;
        });
    });
    compare_both_directions([&](auto& log) {
        return source(log).filter_map([](i32 value) -> Option<i32> {
            if (value % 2_i32 == 0_i32) return None();
            return Some(value * 2_i32);
        });
    });
    compare_both_directions([&](auto& log) {
        return source(log).enumerate().map([](auto pair) {
            return i32(pair.template get<0>().to_primitive()) * 10_i32 + pair.template get<1>();
        });
    });
    compare_both_directions([&](auto& log) {
        return source(log).chain(source(log));
    });
    compare_both_directions([&](auto& log) {
        return source(log)
            .zip(iter::range(10_i32, 13_i32).inspect([&log](i32 v) {
                log.push(i32(v));
            }))
            .map([](auto pair) {
                return pair.template get<0>() + pair.template get<1>();
            });
    });
    compare_both_directions([&](auto& log) {
        return iter::range(10_i32, 13_i32)
            .inspect([&log](i32 v) {
                log.push(i32(v));
            })
            .zip(source(log))
            .map([](auto pair) {
                return pair.template get<0>() + pair.template get<1>();
            });
    });
    for (usize limit; limit < 9_usize; ++limit) {
        compare_both_directions([&](auto& log) {
            return source(log).take(limit);
        });
        compare_both_directions([&](auto& log) {
            return source(log).skip(limit);
        });
        compare_both_directions([&](auto& log) {
            return source(log).skip(limit).take(2_usize);
        });
    }
}

struct ConsumptionGaps : rstd::DefaultInClass<ConsumptionGaps, iter::Iterator> {
    using Item                                = i32;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    int                   front               = 0;
    int                   back                = 0;
    auto next() -> Option<i32> { return ++front % 3 == 1 ? None<i32>() : Some(i32(front)); }
    auto next_back() -> Option<i32> { return ++back % 3 == 1 ? None<i32>() : Some(i32(back)); }
};

TEST(IterConsumption, NonFusedConsumptionRetainsResumeState) {
    auto forward = iter::empty<i32>().chain(ConsumptionGaps {});
    EXPECT_EQ(forward.try_fold(0_i32,
                               [](i32 a, i32 b) {
                                   return Some(a + b);
                               }),
              Some(0_i32));
    EXPECT_EQ(forward.next(), Some(2_i32));
    auto backward = ConsumptionGaps {}.chain(iter::empty<i32>());
    EXPECT_EQ(backward.try_rfold(0_i32,
                                 [](i32 a, i32 b) {
                                     return Some(a + b);
                                 }),
              Some(0_i32));
    EXPECT_EQ(backward.next_back(), Some(2_i32));
    for (usize n; n < 5_usize; ++n) {
        auto actual   = ConsumptionGaps {}.take(n);
        auto expected = ConsumptionGaps {}.take(n);
        EXPECT_EQ(actual.try_fold(0_i32,
                                  [](i32 a, i32 b) {
                                      return Some(a + b);
                                  }),
                  Some(0_i32));
        EXPECT_TRUE(expected.next().is_none());
        for (int i = 0; i < 8; ++i) EXPECT_EQ(actual.next(), expected.next());
        auto skipped     = ConsumptionGaps {}.skip(n);
        auto skipped_ref = ConsumptionGaps {}.skip(n);
        EXPECT_EQ(skipped.try_fold(0_i32,
                                   [](i32 a, i32 b) {
                                       return Some(a + b);
                                   }),
                  Some(0_i32));
        EXPECT_TRUE(skipped_ref.next().is_none());
        for (int i = 0; i < 8; ++i) EXPECT_EQ(skipped.next(), skipped_ref.next());
    }
}

TEST(IterConsumption, MutableByteCopyAndCloneDriversReturnValues) {
    Vec<u8> values;
    for (usize i; i < 6_usize; ++i) values.push(u8(i.to_primitive()));
    compare_both_directions([&](auto& log) {
        return values.iter_mut().copied().map([&log](u8 value) {
            auto v = i32(value.to_primitive());
            log.push(i32(v));
            return v;
        });
    });
    compare_both_directions([&](auto& log) {
        return values.iter_mut().cloned().map([&log](u8 value) {
            auto v = i32(value.to_primitive());
            log.push(i32(v));
            return v;
        });
    });
}

struct ConsumptionClone : rstd::DefaultInClass<ConsumptionClone, rstd::clone::Clone> {
    i32  value;
    int* clones;
    ConsumptionClone(i32 v, int* count): value(v), clones(count) {}
    ConsumptionClone(const ConsumptionClone&)               = delete;
    ConsumptionClone(ConsumptionClone&&)                    = default;
    auto operator=(ConsumptionClone&&) -> ConsumptionClone& = default;
    auto clone() const -> ConsumptionClone {
        ++*clones;
        return ConsumptionClone(value, clones);
    }
};

TEST(IterConsumption, CloneSideEffectsAndResidualTypes) {
    int              calls  = 0;
    ConsumptionClone data[] = { { 1_i32, &calls }, { 2_i32, &calls }, { 3_i32, &calls } };
    auto             source = iter::from_array(data).cloned();
    auto result = source.try_rfold(0_i32, [](i32 sum, ConsumptionClone value) -> Result<i32, i32> {
        if (value.value == 2_i32) return Err(value.value);
        return Ok(sum + value.value);
    });
    EXPECT_EQ(result.unwrap_err(), 2_i32);
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(source.next()->value, 1_i32);
    EXPECT_EQ(calls, 3);
    auto flow_source = iter::range(0_i32, 5_i32).take(4_usize).skip(1_usize);
    auto flow        = flow_source.try_rfold(0_i32, [](i32 sum, i32 v) {
        using Flow = rstd::ops::ControlFlow<i32, i32>;
        return v == 2_i32 ? Flow::Break(v) : Flow::Continue(sum + v);
    });
    EXPECT_TRUE(flow.is_break());
    EXPECT_EQ(flow.break_value_unchecked(), 2_i32);
    EXPECT_EQ(flow_source.next(), Some(1_i32));
}

constexpr auto consumption_constexpr() -> bool {
    const int data[]   = { 1, 2, 3, 4 };
    auto      iterator = iter::from_array(data).copied().filter_map([](int value) {
        return value % 2 ? Some(value) : None<int>();
    });
    auto      found    = iterator.find_map([](int value) {
        return value == 1 ? Some(value) : None<int>();
    });
    if (found.is_none() || *found != 1) return false;
    auto total = iterator.by_ref().rfold(0, [](int a, int b) {
        return a + b;
    });
    if (total != 3 || iterator.next().is_some()) return false;
    auto indexed = iter::from_array(data).copied().enumerate().skip(1_usize).take(2_usize);
    auto sum     = rstd::move(indexed).fold(0, [](int acc, auto pair) {
        return acc + pair.template get<1>();
    });
    return sum == 5;
}
static_assert(consumption_constexpr());
TEST(IterConsumption, ConstexprConsumption) {
    EXPECT_TRUE(consumption_constexpr());
}

TEST(IterConsumption, QueriesPreserveMutablePredicateAndRemainder) {
    for (int stop = 0; stop < 5; ++stop) {
        const int data[] = { 0, 1, 2, 3 };
        auto      source = iter::from_array(data).copied();
        int       calls  = 0;
        auto      found  = source.find([&](int& value) {
            ++calls;
            if (value != stop) return false;
            value += 10;
            return true;
        });
        EXPECT_EQ(calls, stop < 4 ? stop + 1 : 4);
        EXPECT_EQ(found, stop < 4 ? Some(stop + 10) : None<int>());
        EXPECT_EQ(source.next(), stop < 3 ? Some(stop + 1) : None<int>());
        auto reverse  = iter::from_array(data).copied();
        auto position = reverse.rposition([&](int value) {
            return value == stop;
        });
        EXPECT_EQ(position, stop < 4 ? Some(usize(stop)) : None<usize>());
        EXPECT_EQ(reverse.next_back(), stop > 0 && stop < 4 ? Some(stop - 1) : None<int>());
    }
    auto source = iter::from_slice(slice<i32> {}).copied();
    EXPECT_EQ(source.by_ref().fold(7_i32,
                                   [](i32 a, i32 b) {
                                       return a + b;
                                   }),
              7_i32);
    EXPECT_TRUE(source
                    .find([](i32) {
                        return true;
                    })
                    .is_none());
}

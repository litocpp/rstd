#include <gtest/gtest.h>
#include <memory>
import rstd;

using namespace rstd::prelude;
using rstd::Option;
using rstd::Some;
using rstd::None;
using rstd::ref;
using rstd::vec::Vec;
using rstd::string::String;
namespace iter = rstd::iter;
using namespace rstd::literals;

namespace
{

struct ExternalDoubleEnded : DefaultInClass<ExternalDoubleEnded, iter::Iterator> {
    using Item = i32;

    i32 front;
    i32 back;

    ExternalDoubleEnded(i32 first, i32 last): front(first), back(last) {}

    auto next() -> Option<Item> {
        if (front == back) return None();
        auto value = front;
        front += 1_i32;
        return Some(value);
    }

    auto pop_back() -> Option<Item> {
        if (front == back) return None();
        back -= 1_i32;
        return Some(back);
    }

    auto remaining() const -> usize { return rstd::try_from<usize>(back - front).unwrap(); }
};

struct MoveOnlyItem {
    i32 value;

    explicit MoveOnlyItem(i32 value): value(value) {}
    MoveOnlyItem(const MoveOnlyItem&)                    = delete;
    auto operator=(const MoveOnlyItem&) -> MoveOnlyItem& = delete;
    MoveOnlyItem(MoveOnlyItem&&)                         = default;
    auto operator=(MoveOnlyItem&&) -> MoveOnlyItem&      = default;
};

struct RvalueCallable {
    auto operator()() & -> MoveOnlyItem = delete;
    auto operator()() && -> MoveOnlyItem { return MoveOnlyItem(11_i32); }
};

struct WrongNext {
    using Item = i32;
    auto next() -> i32 { return i32(); }
};

struct MemberOnlyIntoIterable {
    auto into_iter() && { return iter::once(1_i32); }
};

struct NonFused : DefaultInClass<NonFused, iter::Iterator> {
    using Item = i32;
    int call {};

    auto next() -> Option<Item> {
        ++call;
        if (call == 1 || call > 2) return None();
        return Some(7_i32);
    }
};

struct NonFusedDoubleEnded : DefaultInClass<NonFusedDoubleEnded, iter::Iterator> {
    using Item                                = i32;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    int                   front_calls {};
    int                   back_calls {};

    auto next() -> Option<Item> {
        ++front_calls;
        return front_calls == 2 ? Some(7_i32) : None();
    }

    auto next_back() -> Option<Item> {
        ++back_calls;
        return back_calls == 2 ? Some(8_i32) : None();
    }

    auto size_hint() const -> iter::SizeHint {
        auto length = front_calls == 1 || back_calls == 1 ? 1_usize : usize();
        return { length, Some(length) };
    }
};

template<class T>
concept CanIntoIter = requires(T&& value) { iter::into_iter(rstd::forward<T>(value)); };

template<class T>
concept CanCallMemberIntoIter = requires(T&& value) { rstd::forward<T>(value).into_iter(); };

template<class T>
concept CanFlatten = requires(T&& value) { rstd::forward<T>(value).flatten(); };

template<class T>
concept CanRev = requires(T&& value) { rstd::forward<T>(value).rev(); };

template<class T>
concept CanCopyItems = requires(T&& value) { rstd::forward<T>(value).copied(); };

template<class T>
concept CanCloneItems = requires(T&& value) { rstd::forward<T>(value).cloned(); };

struct MemberOnlyIterator {
    using Item = i32;

    i32 value;

    auto next() -> Option<Item> {
        if (value == 3_i32) return None();
        auto current = value;
        value += 1_i32;
        return Some(current);
    }
};

template<typename T>
concept ConstRangeFor = requires(const T& value) {
    value.begin();
    value.end();
};

} // namespace

namespace rstd
{

template<>
struct Impl<iter::DoubleEndedIterator, ExternalDoubleEnded> : ImplBase<ExternalDoubleEnded> {
    auto next_back() -> Option<i32> { return this->self().pop_back(); }
};

template<>
struct Impl<iter::ExactSizeIterator, ExternalDoubleEnded> : ImplBase<ExternalDoubleEnded> {
    auto len() const -> usize { return this->self().remaining(); }
};

} // namespace rstd

TEST(Iter, RangeCollect) {
    auto v = iter::range(0_i32, 5_i32).collect<Vec<i32>>();
    ASSERT_EQ(v.len(), 5_usize);
    for (auto i = i32(); i < 5_i32; i += 1_i32) {
        EXPECT_EQ(v[rstd::try_from<usize>(i).unwrap()], i);
    }
}

TEST(Iter, LanguageRangeForConsumesIterator) {
    auto sum = i32();
    for (auto value : iter::range(0_i32, 5_i32)) sum += value;
    EXPECT_EQ(sum, 10_i32);

    auto iterator = iter::range(0_i32, 5_i32);
    for (auto value : iterator) {
        if (value == 1_i32) break;
    }
    EXPECT_EQ(iterator.next(), Some(2_i32));

    auto moved = i32();
    for (auto value : iter::once(MoveOnlyItem(7_i32))) moved = value.value;
    EXPECT_EQ(moved, 7_i32);

    static_assert(rstd::Impled<iter::Range<i32>, iter::IntoIterator>);
    static_assert(! ConstRangeFor<iter::Range<i32>>);
}

TEST(Iter, ForRangeSupportsExternalMemberIterator) {
    auto external = MemberOnlyIterator { 0_i32 };
    auto sum      = i32();
    for (auto value : iter::for_range(external)) sum += value;
    EXPECT_EQ(sum, 3_i32);

    auto collected = MemberOnlyIterator { 0_i32 };
    auto values    = rstd::as<iter::Iterator>(collected).collect<Vec<i32>>();
    ASSERT_EQ(values.len(), 3_usize);
    EXPECT_EQ(values[usize()], 0_i32);
    EXPECT_EQ(values[usize(1)], 1_i32);
    EXPECT_EQ(values[usize(2)], 2_i32);
}

TEST(Iter, LanguageRangeForPreservesBorrowedItems) {
    Vec<i32> values;
    values.push(1_i32);
    values.push(2_i32);
    values.push(3_i32);

    auto sum = i32();
    for (auto value : values.iter()) sum += *value;
    EXPECT_EQ(sum, 6_i32);

    for (auto value : values.iter_mut()) *value += 10_i32;
    EXPECT_EQ(values[0_usize], 11_i32);
    EXPECT_EQ(values[2_usize], 13_i32);

    auto indexed = i32();
    for (auto [index, value] : values.iter().enumerate()) {
        indexed += rstd::try_from<i32>(index).unwrap() * *value;
    }
    EXPECT_EQ(indexed, 38_i32);
}

TEST(Iter, RangeSumCountFold) {
    EXPECT_EQ(iter::range(1_i32, 5_i32).sum(), 10_i32);
    EXPECT_EQ(iter::range(0_i32, 7_i32).count(), 7_usize);
    auto s = iter::range(1_i32, 5_i32).fold(0_i32, [](i32 a, i32 b) {
        return a + b;
    });
    EXPECT_EQ(s, 10_i32);
}

TEST(Iter, MapFilterChain) {
    auto v = iter::range(0_i32, 10_i32)
                 .map([](i32 x) {
                     return x * 2_i32;
                 })
                 .filter([](const i32& x) {
                     return x > 4_i32;
                 })
                 .collect<Vec<i32>>();
    // 0..10 -> *2 -> 0,2,4,6,8,10,12,14,16,18 -> >4 -> 6,8,10,12,14,16,18
    ASSERT_EQ(v.len(), 7_usize);
    EXPECT_EQ(v[0_usize], 6_i32);
    EXPECT_EQ(v[6_usize], 18_i32);
}

TEST(Iter, MapPreservesIteratorCapabilities) {
    auto calls  = i32();
    auto mapped = iter::range(0_i32, 5_i32).map([&](i32 x) {
        calls += 1_i32;
        return rstd::tuple<i32, i32>(x, calls);
    });

    EXPECT_EQ(calls, i32());
    EXPECT_EQ(mapped.size_hint().template get<0>(), 5_usize);
    EXPECT_EQ(mapped.size_hint().template get<1>(), Some(5_usize));
    EXPECT_EQ(mapped.len(), 5_usize);

    auto back = mapped.next_back();
    ASSERT_TRUE(back.is_some());
    EXPECT_EQ(back->get<0>(), 4_i32);
    EXPECT_EQ(back->get<1>(), 1_i32);

    auto front = mapped.next();
    ASSERT_TRUE(front.is_some());
    EXPECT_EQ(front->get<0>(), 0_i32);
    EXPECT_EQ(front->get<1>(), 2_i32);
    EXPECT_EQ(mapped.len(), 3_usize);
    EXPECT_EQ(mapped.size_hint().template get<0>(), 3_usize);

    auto next_back = rstd::as<iter::DoubleEndedIterator>(mapped).next_back();
    ASSERT_TRUE(next_back.is_some());
    EXPECT_EQ(next_back->get<0>(), 3_i32);
    EXPECT_EQ(next_back->get<1>(), 3_i32);
    EXPECT_EQ(rstd::as<iter::ExactSizeIterator>(mapped).len(), 2_usize);
}

TEST(Iter, MapUsesInnerTraitInterfaces) {
    auto mapped = ExternalDoubleEnded { 0_i32, 4_i32 }.map([](i32 x) {
        return x * 10_i32;
    });
    static_assert(rstd::Impled<decltype(mapped), iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<decltype(mapped), iter::ExactSizeIterator>);

    EXPECT_EQ(mapped.next(), Some(0_i32));
    EXPECT_EQ(mapped.next_back(), Some(30_i32));
    EXPECT_EQ(mapped.len(), 2_usize);

    auto source       = iter::from_fn([]() -> Option<i32> {
        return None();
    });
    auto forward_only = rstd::move(source).map([](i32 x) {
        return x;
    });
    static_assert(! rstd::Impled<decltype(forward_only), iter::DoubleEndedIterator>);
    static_assert(! rstd::Impled<decltype(forward_only), iter::ExactSizeIterator>);
}

TEST(Iter, CapabilitiesAreExplicitAndPropagateByContract) {
    using Range        = decltype(iter::range(0_i32, 4_i32));
    using Map          = decltype(iter::range(0_i32, 4_i32).map([](i32 value) {
        return value;
    }));
    using Filter       = decltype(iter::range(0_i32, 4_i32).filter([](const i32&) {
        return true;
    }));
    using Take         = decltype(iter::range(0_i32, 4_i32).take(2_usize));
    using FromFn       = decltype(iter::from_fn([]() -> Option<i32> {
        return None();
    }));
    using Enumerate    = decltype(iter::range(0_i32, 4_i32).enumerate());
    using Zip          = decltype(iter::range(0_i32, 4_i32).zip(iter::range(0_i32, 3_i32)));
    using Skip         = decltype(iter::range(0_i32, 4_i32).skip(1_usize));
    using StepBy       = decltype(iter::range(0_i32, 4_i32).step_by(2_usize));
    using Chain        = decltype(iter::range(0_i32, 2_i32).chain(iter::range(2_i32, 4_i32)));
    using Copied       = decltype(rstd::mtp::declval<Vec<i32>&>().iter().copied());
    using Inspect      = decltype(iter::range(0_i32, 4_i32).inspect([](const i32&) {
    }));
    using Fuse         = decltype(iter::from_fn([]() -> Option<i32> {
                              return None();
                                  }).fuse());
    using Peekable     = decltype(iter::range(0_i32, 4_i32).peekable());
    using Rev          = decltype(iter::range(0_i32, 4_i32).rev());
    using Flatten      = decltype(iter::once(iter::range(0_i32, 2_i32)).flatten());
    using PointerItems = decltype(iter::once(static_cast<i32*>(nullptr)));

    static_assert(iter::has_next<Range>);
    static_assert(! iter::has_next<WrongNext>);
    static_assert(! ConstRangeFor<Range>);
    static_assert(! CanCallMemberIntoIter<Range&>);
    static_assert(CanCallMemberIntoIter<Range>);
    static_assert(! rstd::mtp::copy<iter::IteratorLoop<Range>>);
    static_assert(rstd::Impled<Range, iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<Range, iter::ExactSizeIterator>);
    static_assert(rstd::Impled<Range, iter::FusedIterator>);
    static_assert(rstd::Impled<Range, iter::TrustedLen>);
    static_assert(rstd::Impled<Map, iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<Map, iter::ExactSizeIterator>);
    static_assert(rstd::Impled<Map, iter::FusedIterator>);
    static_assert(rstd::Impled<Map, iter::TrustedLen>);
    static_assert(rstd::Impled<Filter, iter::DoubleEndedIterator>);
    static_assert(! rstd::Impled<Filter, iter::ExactSizeIterator>);
    static_assert(rstd::Impled<Filter, iter::FusedIterator>);
    static_assert(! rstd::Impled<Filter, iter::TrustedLen>);
    static_assert(rstd::Impled<Take, iter::ExactSizeIterator>);
    static_assert(rstd::Impled<Take, iter::TrustedLen>);
    static_assert(rstd::Impled<Enumerate, iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<Enumerate, iter::ExactSizeIterator>);
    static_assert(rstd::Impled<Enumerate, iter::TrustedLen>);
    static_assert(rstd::Impled<Zip, iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<Zip, iter::ExactSizeIterator>);
    static_assert(rstd::Impled<Zip, iter::TrustedLen>);
    static_assert(rstd::Impled<Skip, iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<Skip, iter::ExactSizeIterator>);
    static_assert(! rstd::Impled<Skip, iter::TrustedLen>);
    static_assert(rstd::Impled<StepBy, iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<StepBy, iter::ExactSizeIterator>);
    static_assert(! rstd::Impled<StepBy, iter::TrustedLen>);
    static_assert(rstd::Impled<Chain, iter::DoubleEndedIterator>);
    static_assert(! rstd::Impled<Chain, iter::ExactSizeIterator>);
    static_assert(rstd::Impled<Chain, iter::TrustedLen>);
    static_assert(rstd::Impled<Copied, iter::DoubleEndedIterator>);
    static_assert(! CanCopyItems<PointerItems>);
    static_assert(! CanCloneItems<PointerItems>);
    static_assert(rstd::Impled<Copied, iter::ExactSizeIterator>);
    static_assert(rstd::Impled<Copied, iter::TrustedLen>);
    static_assert(rstd::Impled<Inspect, iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<Fuse, iter::FusedIterator>);
    static_assert(rstd::Impled<Peekable, iter::TrustedLen>);
    static_assert(rstd::Impled<Rev, iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<Rev, iter::TrustedLen>);
    static_assert(rstd::Impled<Flatten, iter::DoubleEndedIterator>);
    static_assert(rstd::Impled<Flatten, iter::FusedIterator>);
    static_assert(! rstd::Impled<Flatten, iter::ExactSizeIterator>);
    static_assert(! rstd::Impled<Flatten, iter::TrustedLen>);
    static_assert(! rstd::Impled<FromFn, iter::FusedIterator>);

    static_assert(CanIntoIter<Range>);
    static_assert(! CanIntoIter<Range&>);
    static_assert(! CanFlatten<decltype(iter::once(MemberOnlyIntoIterable()))>);
    static_assert(! CanRev<FromFn>);
}

TEST(Iter, SizeHintsTrackPartialConsumption) {
    auto expect_exact = [](const iter::SizeHint& hint, usize expected) {
        EXPECT_EQ(hint.template get<0>(), expected);
        EXPECT_EQ(hint.template get<1>(), Some(expected));
    };

    auto take = iter::range(0_i32, 5_i32).take(3_usize);
    expect_exact(take.size_hint(), 3_usize);
    (void)take.next();
    expect_exact(take.size_hint(), 2_usize);

    auto skip = iter::range(0_i32, 5_i32).skip(2_usize);
    expect_exact(skip.size_hint(), 3_usize);
    (void)skip.next();
    expect_exact(skip.size_hint(), 2_usize);

    auto zip = iter::range(0_i32, 3_i32).zip(iter::range(0_i32, 5_i32));
    expect_exact(zip.size_hint(), 3_usize);
    (void)zip.next();
    expect_exact(zip.size_hint(), 2_usize);

    auto step = iter::range(0_i32, 10_i32).step_by(3_usize);
    expect_exact(step.size_hint(), 4_usize);
    (void)step.next();
    expect_exact(step.size_hint(), 3_usize);

    auto filter = iter::range(0_i32, 5_i32).filter([](const i32& value) {
        return value % 2_i32 == i32();
    });
    EXPECT_EQ(filter.size_hint().template get<0>(), usize());
    EXPECT_EQ(filter.size_hint().template get<1>(), Some(5_usize));
    (void)filter.next();
    EXPECT_EQ(filter.size_hint().template get<1>(), Some(4_usize));

    auto interspersed = iter::range(0_i32, 3_i32).intersperse(9_i32);
    expect_exact(interspersed.size_hint(), 5_usize);
    (void)interspersed.next();
    expect_exact(interspersed.size_hint(), 4_usize);
}

TEST(Iter, NonFusedAdaptersRetainRustResumeSemantics) {
    auto take_while = NonFused {}.take_while([](const i32&) {
        return true;
    });
    static_assert(! rstd::Impled<decltype(take_while), iter::FusedIterator>);
    EXPECT_TRUE(take_while.next().is_none());
    EXPECT_EQ(take_while.next(), Some(7_i32));

    auto peekable = NonFused {}.peekable();
    EXPECT_EQ(peekable.peek(), nullptr);
    EXPECT_TRUE(peekable.next().is_none());
    EXPECT_EQ(peekable.next(), Some(7_i32));

    auto interspersed = NonFused {}.intersperse(9_i32);
    static_assert(! rstd::Impled<decltype(interspersed), iter::FusedIterator>);
    EXPECT_TRUE(interspersed.next().is_none());
    EXPECT_TRUE(interspersed.next().is_none());

    auto flattened = NonFused {}
                         .map([](i32 value) {
                             return iter::once(value);
                         })
                         .flatten();
    EXPECT_TRUE(flattened.next().is_none());
    EXPECT_TRUE(flattened.next().is_none());

    auto cycle = NonFused {}.cycle();
    static_assert(rstd::Impled<decltype(cycle), iter::FusedIterator>);
    EXPECT_TRUE(cycle.next().is_none());
    EXPECT_EQ(cycle.next(), Some(7_i32));

    auto cleared_front = NonFusedDoubleEnded {}.chain(iter::once(1_i32));
    EXPECT_EQ(cleared_front.next(), Some(1_i32));
    EXPECT_TRUE(cleared_front.next_back().is_none());
    EXPECT_EQ(cleared_front.size_hint(), iter::SizeHint(usize(), Some(usize())));

    auto cleared_back = iter::once(1_i32).chain(NonFusedDoubleEnded {});
    EXPECT_EQ(cleared_back.next_back(), Some(1_i32));
    EXPECT_TRUE(cleared_back.next().is_none());
    EXPECT_EQ(cleared_back.size_hint(), iter::SizeHint(usize(), Some(usize())));
}

static_assert([] {
    auto sum = i32();
    for (auto value : iter::range(0_i32, 4_i32)) sum += value;
    return sum == 6_i32;
}());

TEST(Iter, DoubleEndedAdaptersPreserveMixedFrontBackState) {
    auto filtered = iter::range(0_i32, 8_i32).filter([](const i32& value) {
        return value % 2_i32 == i32();
    });
    EXPECT_EQ(filtered.next(), Some(0_i32));
    EXPECT_EQ(filtered.next_back(), Some(6_i32));
    EXPECT_EQ(filtered.next_back(), Some(4_i32));
    EXPECT_EQ(filtered.next(), Some(2_i32));
    EXPECT_TRUE(filtered.next().is_none());

    auto zipped = iter::range(0_i32, 5_i32).zip(iter::range(10_i32, 13_i32));
    EXPECT_EQ(zipped.next_back(), Some(rstd::tuple<i32, i32>(2_i32, 12_i32)));
    EXPECT_EQ(zipped.next(), Some(rstd::tuple<i32, i32>(0_i32, 10_i32)));
    EXPECT_EQ(zipped.len(), 1_usize);

    auto taken = iter::range(0_i32, 8_i32).take(5_usize);
    EXPECT_EQ(taken.next(), Some(0_i32));
    EXPECT_EQ(taken.next_back(), Some(4_i32));
    EXPECT_EQ(taken.len(), 3_usize);

    auto skipped = iter::range(0_i32, 8_i32).skip(3_usize);
    EXPECT_EQ(skipped.next_back(), Some(7_i32));
    EXPECT_EQ(skipped.next(), Some(3_i32));
    EXPECT_EQ(skipped.len(), 3_usize);

    auto stepped = iter::range(0_i32, 10_i32).step_by(3_usize);
    EXPECT_EQ(stepped.next(), Some(0_i32));
    EXPECT_EQ(stepped.next_back(), Some(9_i32));
    EXPECT_EQ(stepped.next_back(), Some(6_i32));
    EXPECT_EQ(stepped.next(), Some(3_i32));
    EXPECT_TRUE(stepped.next().is_none());

    auto chained = iter::range(0_i32, 2_i32).chain(iter::range(10_i32, 12_i32));
    EXPECT_EQ(chained.next(), Some(0_i32));
    EXPECT_EQ(chained.next_back(), Some(11_i32));
    EXPECT_EQ(chained.next_back(), Some(10_i32));
    EXPECT_EQ(chained.next(), Some(1_i32));
}

TEST(Iter, PeekableAndFuseKeepTheirStateContracts) {
    auto peekable = iter::range(0_i32, 3_i32).peekable();
    EXPECT_EQ(peekable.len(), 3_usize);
    ASSERT_NE(peekable.peek(), nullptr);
    EXPECT_EQ(peekable.len(), 3_usize);
    EXPECT_EQ(peekable.next_back(), Some(2_i32));
    EXPECT_EQ(peekable.next(), Some(0_i32));
    EXPECT_EQ(peekable.next_back(), Some(1_i32));
    EXPECT_TRUE(peekable.next().is_none());

    auto non_fused = NonFused {};
    EXPECT_TRUE(non_fused.next().is_none());
    EXPECT_EQ(non_fused.next(), Some(7_i32));

    auto fused = NonFused {}.fuse();
    static_assert(rstd::Impled<decltype(fused), iter::FusedIterator>);
    EXPECT_TRUE(fused.next().is_none());
    EXPECT_TRUE(fused.next().is_none());
}

TEST(Iter, StepByRejectsZero) {
    EXPECT_DEATH((void)iter::range(0_i32, 4_i32).step_by(usize()), "step_by called with step 0");
}

TEST(Iter, TakeSkipStepBy) {
    auto a = iter::range(0_i32, 100_i32).take(3_usize).collect<Vec<i32>>();
    ASSERT_EQ(a.len(), 3_usize);
    EXPECT_EQ(a[2_usize], 2_i32);

    auto b = iter::range(0_i32, 5_i32).skip(2_usize).collect<Vec<i32>>();
    ASSERT_EQ(b.len(), 3_usize);
    EXPECT_EQ(b[0_usize], 2_i32);

    auto c = iter::range(0_i32, 10_i32).step_by(3_usize).collect<Vec<i32>>();
    ASSERT_EQ(c.len(), 4_usize); // 0,3,6,9
    EXPECT_EQ(c[1_usize], 3_i32);
}

TEST(Iter, EnumerateZipChain) {
    auto e = iter::range(10_i32, 13_i32).enumerate().collect<Vec<rstd::tuple<usize, i32>>>();
    ASSERT_EQ(e.len(), 3_usize);
    EXPECT_EQ(e[1_usize].get<0>(), 1_usize);
    EXPECT_EQ(e[1_usize].get<1>(), 11_i32);

    auto z = iter::range(0_i32, 3_i32)
                 .zip(iter::range(100_i32, 105_i32))
                 .collect<Vec<rstd::tuple<i32, i32>>>();
    ASSERT_EQ(z.len(), 3_usize);
    EXPECT_EQ(z[2_usize].get<1>(), 102_i32);

    auto c = iter::range(0_i32, 2_i32).chain(iter::range(10_i32, 12_i32)).collect<Vec<i32>>();
    ASSERT_EQ(c.len(), 4_usize);
    EXPECT_EQ(c[2_usize], 10_i32);
}

TEST(Iter, FindAnyAllPosition) {
    EXPECT_TRUE(iter::range(0_i32, 5_i32).any([](const i32& x) {
        return x == 3_i32;
    }));
    EXPECT_FALSE(iter::range(0_i32, 5_i32).all([](const i32& x) {
        return x < 3_i32;
    }));
    auto p = iter::range(0_i32, 5_i32).position([](const i32& x) {
        return x == 2_i32;
    });
    EXPECT_EQ(p, Some(2_usize));
    auto f = iter::range(0_i32, 5_i32).find([](const i32& x) {
        return x > 2_i32;
    });
    EXPECT_EQ(f, Some(3_i32));
}

TEST(Iter, Sources) {
    auto o = iter::once(7_i32).collect<Vec<i32>>();
    ASSERT_EQ(o.len(), 1_usize);
    EXPECT_EQ(o[0_usize], 7_i32);

    auto r = iter::repeat(9_i32).take(3_usize).collect<Vec<i32>>();
    ASSERT_EQ(r.len(), 3_usize);
    EXPECT_EQ(r[2_usize], 9_i32);

    auto em = iter::empty<i32>().collect<Vec<i32>>();
    EXPECT_EQ(em.len(), 0_usize);
}

TEST(Iter, OnceWithIsLazyAndSupportsMoveOnlyClosureState) {
    auto calls = 0;
    auto state = std::make_unique<int>(7);
    auto once  = iter::once_with([state = std::move(state), &calls]() mutable {
        ++calls;
        return MoveOnlyItem(i32(*state));
    });

    EXPECT_EQ(calls, 0);
    EXPECT_EQ(once.len(), 1_usize);
    auto value = once.next();
    ASSERT_TRUE(value.is_some());
    EXPECT_EQ(value->value, 7_i32);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(once.next().is_none());
    EXPECT_EQ(calls, 1);

    auto once_callable = iter::once_with(RvalueCallable {});
    auto item          = once_callable.next();
    ASSERT_TRUE(item.is_some());
    EXPECT_EQ(item->value, 11_i32);
}

TEST(Iter, RepeatWithMaintainsMoveOnlyClosureState) {
    auto state  = std::make_unique<int>(2);
    auto values = iter::repeat_with([state = std::move(state)]() mutable {
                      return i32((*state)++);
                  })
                      .take(3_usize)
                      .collect<Vec<i32>>();
    ASSERT_EQ(values.len(), 3_usize);
    EXPECT_EQ(values[0_usize], 2_i32);
    EXPECT_EQ(values[1_usize], 3_i32);
    EXPECT_EQ(values[2_usize], 4_i32);
}

TEST(Iter, SuccessorsStopsPermanentlyAndSupportsMoveOnlyValues) {
    auto calls = 0;
    auto values =
        iter::successors(rstd::Some(MoveOnlyItem(1_i32)), [&calls](const MoveOnlyItem& item) {
            ++calls;
            if (item.value == 3_i32) return rstd::Option<MoveOnlyItem>(rstd::None());
            return rstd::Some(MoveOnlyItem(item.value + 1_i32));
        });

    EXPECT_EQ(values.next()->value, 1_i32);
    EXPECT_EQ(values.next()->value, 2_i32);
    EXPECT_EQ(values.next()->value, 3_i32);
    EXPECT_TRUE(values.next().is_none());
    EXPECT_TRUE(values.next().is_none());
    EXPECT_EQ(calls, 3);
}

TEST(Iter, VecIterCopied) {
    Vec<i32> v;
    v.push(1_i32);
    v.push(2_i32);
    v.push(3_i32);
    auto doubled = v.iter()
                       .copied()
                       .map([](i32 x) {
                           return x * 10_i32;
                       })
                       .collect<Vec<i32>>();
    ASSERT_EQ(doubled.len(), 3_usize);
    EXPECT_EQ(doubled[0_usize], 10_i32);
    EXPECT_EQ(doubled[2_usize], 30_i32);

    // borrow sum
    auto total = i32();
    v.iter().for_each([&](ref<i32> x) {
        total += *x;
    });
    EXPECT_EQ(total, 6_i32);
}

TEST(Iter, VecIntoIter) {
    Vec<i32> v;
    v.push(5_i32);
    v.push(6_i32);
    v.push(7_i32);
    auto out = v.into_iter()
                   .filter([](const i32& x) {
                       return x != 6_i32;
                   })
                   .collect<Vec<i32>>();
    ASSERT_EQ(out.len(), 2_usize);
    EXPECT_EQ(out[0_usize], 5_i32);
    EXPECT_EQ(out[1_usize], 7_i32);
}

TEST(Iter, RevCycle) {
    auto r = iter::range(0_i32, 5_i32).rev().collect<Vec<i32>>();
    ASSERT_EQ(r.len(), 5_usize);
    EXPECT_EQ(r[0_usize], 4_i32);
    EXPECT_EQ(r[4_usize], 0_i32);

    auto c = iter::range(0_i32, 3_i32).cycle().take(7_usize).collect<Vec<i32>>();
    ASSERT_EQ(c.len(), 7_usize);
    EXPECT_EQ(c[3_usize], 0_i32);
    EXPECT_EQ(c[6_usize], 0_i32);
}

TEST(Iter, CycleSizeHintUsesTheOriginalIterator) {
    auto cycle = iter::range(0_i32, 2_i32).cycle();
    EXPECT_EQ(cycle.size_hint(), iter::SizeHint(usize::MAX, None()));
    EXPECT_EQ(cycle.next(), Some(0_i32));
    EXPECT_EQ(cycle.next(), Some(1_i32));
    EXPECT_EQ(cycle.size_hint(), iter::SizeHint(usize::MAX, None()));

    auto empty = iter::empty<i32>().cycle();
    EXPECT_EQ(empty.size_hint(), iter::SizeHint(usize(), Some(usize())));
}

TEST(Iter, TakeWhileSkipWhile) {
    auto a = iter::range(0_i32, 10_i32)
                 .take_while([](const i32& x) {
                     return x < 5_i32;
                 })
                 .collect<Vec<i32>>();
    ASSERT_EQ(a.len(), 5_usize);
    EXPECT_EQ(a[4_usize], 4_i32);

    auto b = iter::range(0_i32, 10_i32)
                 .skip_while([](const i32& x) {
                     return x < 5_i32;
                 })
                 .collect<Vec<i32>>();
    ASSERT_EQ(b.len(), 5_usize);
    EXPECT_EQ(b[0_usize], 5_i32);
}

TEST(Iter, FilterMapMapWhile) {
    auto a = iter::range(0_i32, 6_i32)
                 .filter_map([](i32 x) -> Option<i32> {
                     if (x % 2_i32 == i32()) return Some(x * 10_i32);
                     return None();
                 })
                 .collect<Vec<i32>>();
    ASSERT_EQ(a.len(), 3_usize); // 0,20,40
    EXPECT_EQ(a[1_usize], 20_i32);

    auto b = iter::range(0_i32, 10_i32)
                 .map_while([](i32 x) -> Option<i32> {
                     if (x < 3_i32) return Some(x);
                     return None();
                 })
                 .collect<Vec<i32>>();
    ASSERT_EQ(b.len(), 3_usize);
    EXPECT_EQ(b[2_usize], 2_i32);
}

TEST(Iter, ScanIntersperse) {
    auto a = iter::range(1_i32, 5_i32)
                 .scan(0_i32,
                       [](i32& st, i32 x) -> Option<i32> {
                           st += x;
                           return Some(st);
                       })
                 .collect<Vec<i32>>();
    ASSERT_EQ(a.len(), 4_usize); // 1,3,6,10
    EXPECT_EQ(a[3_usize], 10_i32);

    auto b = iter::range(1_i32, 4_i32).intersperse(0_i32).collect<Vec<i32>>();
    ASSERT_EQ(b.len(), 5_usize); // 1,0,2,0,3
    EXPECT_EQ(b[1_usize], 0_i32);
    EXPECT_EQ(b[2_usize], 2_i32);
}

TEST(Iter, FlatMapFlatten) {
    auto a = iter::range(0_i32, 4_i32)
                 .flat_map([](i32 x) {
                     return iter::range(i32(), x);
                 })
                 .collect<Vec<i32>>();
    // 0:[],1:[0],2:[0,1],3:[0,1,2] -> 0,0,1,0,1,2
    ASSERT_EQ(a.len(), 6_usize);
    EXPECT_EQ(a[0_usize], 0_i32);
    EXPECT_EQ(a[5_usize], 2_i32);

    Vec<rstd::iter::Range<i32>> vr;
    vr.push(iter::range(0_i32, 2_i32));
    vr.push(iter::range(10_i32, 12_i32));
    auto b = vr.into_iter().flatten().collect<Vec<i32>>();
    ASSERT_EQ(b.len(), 4_usize);
    EXPECT_EQ(b[0_usize], 0_i32);
    EXPECT_EQ(b[2_usize], 10_i32);
}

TEST(Iter, FlattenNestedOwnedBorrowedAndMutableCollections) {
    Vec<Vec<i32>> rows;
    for (auto start : { 0_i32, 10_i32, 20_i32 }) {
        Vec<i32> row;
        row.push(start + i32());
        row.push(start + 1_i32);
        rows.push(rstd::move(row));
    }

    auto borrowed = rows.iter().flatten();
    static_assert(rstd::mtp::same_as<typename decltype(borrowed)::Item, ref<i32>>);
    EXPECT_EQ(borrowed.next().unwrap_unchecked().get(), 0_i32);
    EXPECT_EQ(borrowed.next_back().unwrap_unchecked().get(), 21_i32);

    auto mutable_items = rows.iter_mut().flatten();
    static_assert(rstd::mtp::same_as<typename decltype(mutable_items)::Item, rstd::mut_ref<i32>>);
    for (auto value : mutable_items) *value += 1_i32;
    EXPECT_EQ(rows[0_usize][0_usize], 1_i32);
    EXPECT_EQ(rows[2_usize][0_usize], 21_i32);

    auto owned = rstd::move(rows).into_iter().flatten().collect<Vec<i32>>();
    ASSERT_EQ(owned.len(), 6_usize);
    EXPECT_EQ(owned[0_usize], 1_i32);
    EXPECT_EQ(owned[5_usize], 22_i32);
}

TEST(Iter, FlattenSupportsRepeatedNestingAndEmptyInnerIterators) {
    Vec<Vec<Vec<i32>>> groups;
    groups.push(Vec<Vec<i32>>::make());

    Vec<Vec<i32>> rows;
    rows.push(Vec<i32>::make());
    Vec<i32> values;
    values.push(3_i32);
    values.push(5_i32);
    rows.push(rstd::move(values));
    rows.push(Vec<i32>::make());
    groups.push(rstd::move(rows));

    auto flattened = rstd::move(groups).into_iter().flatten().flatten().collect<Vec<i32>>();
    ASSERT_EQ(flattened.len(), 2_usize);
    EXPECT_EQ(flattened[0_usize], 3_i32);
    EXPECT_EQ(flattened[1_usize], 5_i32);
}

TEST(Iter, FlattenPreservesMoveOnlyItemsAndStateAcrossAdapterMoves) {
    Vec<Vec<MoveOnlyItem>> rows;
    Vec<MoveOnlyItem>      first;
    first.push(MoveOnlyItem(1_i32));
    first.push(MoveOnlyItem(2_i32));
    rows.push(rstd::move(first));
    Vec<MoveOnlyItem> second;
    second.push(MoveOnlyItem(3_i32));
    rows.push(rstd::move(second));

    auto flattened = rstd::move(rows).into_iter().flatten();
    EXPECT_EQ(flattened.next()->value, 1_i32);
    auto moved = rstd::move(flattened);
    EXPECT_EQ(moved.next()->value, 2_i32);
    EXPECT_EQ(moved.next()->value, 3_i32);
    EXPECT_TRUE(moved.next().is_none());
}

TEST(Iter, FlattenSupportsArrayAndBoxedSliceOwners) {
    auto arrays = rstd::array<rstd::array<i32, 2>, 2> { rstd::array<i32, 2> { 1_i32, 2_i32 },
                                                        rstd::array<i32, 2> { 3_i32, 4_i32 } };
    auto array_values = rstd::move(arrays).into_iter().flatten().collect<Vec<i32>>();
    ASSERT_EQ(array_values.len(), 4_usize);
    EXPECT_EQ(array_values[3_usize], 4_i32);

    Vec<Vec<i32>> rows;
    Vec<i32>      first;
    first.push(5_i32);
    rows.push(rstd::move(first));
    Vec<i32> second;
    second.push(7_i32);
    second.push(9_i32);
    rows.push(rstd::move(second));
    auto boxed_values =
        rstd::iter::into_iter(rows.into_boxed_slice()).flatten().collect<Vec<i32>>();
    ASSERT_EQ(boxed_values.len(), 3_usize);
    EXPECT_EQ(boxed_values[0_usize], 5_i32);
    EXPECT_EQ(boxed_values[2_usize], 9_i32);
}

TEST(Iter, FlatMapMatchesMapFlattenAndContinuesAfterBreak) {
    auto flat_map_values = iter::range(1_i32, 4_i32)
                               .flat_map([](i32 value) {
                                   return iter::range(i32(), value);
                               })
                               .collect<Vec<i32>>();
    auto flatten_values  = iter::range(1_i32, 4_i32)
                               .map([](i32 value) {
                                  return iter::range(i32(), value);
                               })
                               .flatten()
                               .collect<Vec<i32>>();
    EXPECT_EQ(flat_map_values, flatten_values);

    auto flat_map = iter::range(1_i32, 4_i32).flat_map([](i32 value) {
        return iter::range(i32(), value);
    });
    auto seen     = usize();
    for (auto value : flat_map) {
        (void)value;
        if (++seen == 2_usize) break;
    }
    EXPECT_EQ(flat_map.next(), Some(1_i32));
}

TEST(Iter, OptionAndResultAreZeroOrOneItemIterators) {
    Vec<Option<i32>> options;
    options.push(Some(2_i32));
    options.push(None());
    options.push(Some(7_i32));
    auto option_values = rstd::move(options).into_iter().flatten().collect<Vec<i32>>();
    ASSERT_EQ(option_values.len(), 2_usize);
    EXPECT_EQ(option_values[0_usize], 2_i32);
    EXPECT_EQ(option_values[1_usize], 7_i32);

    Vec<rstd::Result<i32, i32>> results;
    results.push(rstd::Ok(3_i32));
    results.push(rstd::Err(11_i32));
    results.push(rstd::Ok(5_i32));
    auto result_values = rstd::move(results).into_iter().flatten().collect<Vec<i32>>();
    ASSERT_EQ(result_values.len(), 2_usize);
    EXPECT_EQ(result_values[0_usize], 3_i32);
    EXPECT_EQ(result_values[1_usize], 5_i32);

    auto optional = Some(9_i32);
    auto borrowed = ref<Option<i32>>::from_raw_parts(rstd::addressof(optional));
    auto item     = iter::once(borrowed).flatten().next();
    ASSERT_TRUE(item.is_some());
    EXPECT_EQ(**item, 9_i32);

    auto mutable_borrow = rstd::mut_ref<Option<i32>>::from_raw_parts(rstd::addressof(optional));
    auto mutable_item   = iter::once(mutable_borrow).flatten().next();
    ASSERT_TRUE(mutable_item.is_some());
    **mutable_item = 13_i32;
    EXPECT_EQ(*optional, 13_i32);

    auto byte_optional = Some(rstd::u8(3));
    auto byte_borrowed =
        iter::into_iter(ref<Option<rstd::u8>>::from_raw_parts(rstd::addressof(byte_optional)));
    static_assert(rstd::mtp::same_as<typename decltype(byte_borrowed)::Item, const rstd::u8&>);
    EXPECT_EQ(*byte_borrowed.next(), rstd::u8(3));

    auto byte_mutable = iter::into_iter(
        rstd::mut_ref<Option<rstd::u8>>::from_raw_parts(rstd::addressof(byte_optional)));
    static_assert(rstd::mtp::same_as<typename decltype(byte_mutable)::Item, rstd::u8&>);
    auto byte_item = byte_mutable.next();
    ASSERT_TRUE(byte_item.is_some());
    *byte_item = rstd::u8(5);
    EXPECT_EQ(*byte_optional, rstd::u8(5));

    auto result = rstd::Result<i32, i32>(rstd::Ok(17_i32));
    auto borrowed_result =
        iter::once(ref<rstd::Result<i32, i32>>::from_raw_parts(rstd::addressof(result))).flatten();
    EXPECT_EQ(**borrowed_result.next(), 17_i32);

    auto byte_result          = rstd::Result<rstd::u8, i32>(rstd::Ok(rstd::u8(7)));
    auto byte_result_borrowed = iter::into_iter(
        ref<rstd::Result<rstd::u8, i32>>::from_raw_parts(rstd::addressof(byte_result)));
    static_assert(
        rstd::mtp::same_as<typename decltype(byte_result_borrowed)::Item, const rstd::u8&>);
    EXPECT_EQ(*byte_result_borrowed.next(), rstd::u8(7));

    auto mutable_result =
        iter::once(rstd::mut_ref<rstd::Result<i32, i32>>::from_raw_parts(rstd::addressof(result)))
            .flatten();
    **mutable_result.next() = 19_i32;
    EXPECT_EQ(*result, 19_i32);

    auto error = rstd::Result<i32, i32>(rstd::Err(23_i32));
    auto borrowed_error =
        iter::once(ref<rstd::Result<i32, i32>>::from_raw_parts(rstd::addressof(error))).flatten();
    EXPECT_TRUE(borrowed_error.next().is_none());
    EXPECT_EQ(error.unwrap_err_unchecked(), 23_i32);
}

TEST(Iter, PeekableInspectFuse) {
    auto p = iter::range(0_i32, 3_i32).peekable();
    ASSERT_NE(p.peek(), nullptr);
    EXPECT_EQ(*p.peek(), 0_i32);
    EXPECT_EQ(p.next(), Some(0_i32));
    EXPECT_EQ(p.next(), Some(1_i32));

    auto seen = i32();
    auto v    = iter::range(0_i32, 3_i32)
                    .inspect([&](const i32& x) {
                     seen += x;
                    })
                    .collect<Vec<i32>>();
    EXPECT_EQ(seen, 3_i32);
    EXPECT_EQ(v.len(), 3_usize);
}

TEST(Iter, ReduceMinByKeyTryFoldEq) {
    EXPECT_EQ(iter::range(1_i32, 5_i32).reduce([](i32 a, i32 b) {
        return a + b;
    }),
              Some(10_i32));

    auto m = iter::range(0_i32, 5_i32).min_by_key([](i32 x) {
        return (x - 2_i32) * (x - 2_i32);
    });
    EXPECT_EQ(m, Some(2_i32));

    auto tf = iter::range(1_i32, 5_i32).try_fold(0_i32, [](i32 a, i32 b) -> Option<i32> {
        return Some(a + b);
    });
    EXPECT_EQ(tf, Some(10_i32));

    EXPECT_TRUE(iter::range(0_i32, 3_i32).eq(iter::range(0_i32, 3_i32)));
    EXPECT_FALSE(iter::range(0_i32, 3_i32).eq(iter::range(0_i32, 4_i32)));
    EXPECT_TRUE(iter::range(0_i32, 3_i32).lt(iter::range(0_i32, 4_i32)));
    EXPECT_TRUE(iter::range(0_i32, 4_i32).gt(iter::range(0_i32, 3_i32)));
    EXPECT_TRUE(iter::range(0_i32, 3_i32).ne(iter::range(1_i32, 4_i32)));
}

TEST(Iter, VecClonedDoubleEnded) {
    Vec<i32> v;
    v.push(1_i32);
    v.push(2_i32);
    v.push(3_i32);
    auto cl = v.iter().cloned().collect<Vec<i32>>();
    ASSERT_EQ(cl.len(), 3_usize);
    EXPECT_EQ(cl[1_usize], 2_i32);

    // DoubleEndedIterator on SliceIter via trait
    auto it   = v.iter();
    auto back = rstd::as<iter::DoubleEndedIterator>(it).next_back();
    ASSERT_TRUE(back.is_some());
    EXPECT_EQ(**back, 3_i32);
}

TEST(Iter, SliceArray) {
    i32  arr[4] = { 10_i32, 20_i32, 30_i32, 40_i32 };
    auto s      = iter::from_array(arr).copied().collect<Vec<i32>>();
    ASSERT_EQ(s.len(), 4_usize);
    EXPECT_EQ(s[2_usize], 30_i32);

    iter::from_array_mut(arr).for_each([](rstd::mut_ref<i32> x) {
        *x += 1_i32;
    });
    EXPECT_EQ(arr[0], 11_i32);
    EXPECT_EQ(arr[3], 41_i32);
}

TEST(Iter, MinByMaxBy) {
    auto cmp = [](const i32& a, const i32& b) {
        return a <=> b;
    };
    EXPECT_EQ(iter::range(0_i32, 5_i32).min_by(cmp), Some(0_i32));
    EXPECT_EQ(iter::range(0_i32, 5_i32).max_by(cmp), Some(4_i32));
}

TEST(Iter, RpositionNthBackAdvanceBy) {
    auto p = iter::range(0_i32, 5_i32).rposition([](const i32& x) {
        return x < 3_i32;
    });
    EXPECT_EQ(p, Some(2_usize)); // last element < 3 is value 2 at index 2

    EXPECT_EQ(iter::range(0_i32, 5_i32).nth_back(0_usize), Some(4_i32));
    EXPECT_EQ(iter::range(0_i32, 5_i32).nth_back(1_usize), Some(3_i32));

    auto it = iter::range(0_i32, 5_i32);
    EXPECT_EQ(it.advance_by(2_usize), 2_usize);
    EXPECT_EQ(it.next(), Some(2_i32));
    EXPECT_EQ(it.advance_by(100_usize), 2_usize); // only 3,4 remain
}

TEST(Iter, IsSorted) {
    EXPECT_TRUE(iter::range(0_i32, 5_i32).is_sorted());

    Vec<i32> v;
    v.push(3_i32);
    v.push(1_i32);
    v.push(2_i32);
    EXPECT_FALSE(v.iter().copied().is_sorted());

    EXPECT_TRUE(iter::range(0_i32, 5_i32).is_sorted_by_key([](i32 x) {
        return x;
    }));
    EXPECT_FALSE(iter::range(0_i32, 5_i32).is_sorted_by_key([](i32 x) {
        return -x;
    }));
}

TEST(Iter, PartitionUnzip) {
    auto pr = iter::range(0_i32, 6_i32).partition<Vec<i32>>([](const i32& x) {
        return x % 2_i32 == i32();
    });
    ASSERT_EQ(pr.get<0>().len(), 3_usize); // evens
    ASSERT_EQ(pr.get<1>().len(), 3_usize); // odds
    EXPECT_EQ(pr.get<0>()[1_usize], 2_i32);
    EXPECT_EQ(pr.get<1>()[0_usize], 1_i32);

    auto uz = iter::range(0_i32, 3_i32)
                  .map([](i32 x) {
                      return rstd::tuple<i32, i32>(x, x * x);
                  })
                  .unzip<Vec<i32>, Vec<i32>>();
    ASSERT_EQ(uz.get<0>().len(), 3_usize);
    ASSERT_EQ(uz.get<1>().len(), 3_usize);
    EXPECT_EQ(uz.get<0>()[2_usize], 2_i32);
    EXPECT_EQ(uz.get<1>()[2_usize], 4_i32);
}

TEST(Iter, ByRef) {
    auto it        = iter::range(0_i32, 10_i32);
    auto first_two = it.by_ref().take(2_usize).collect<Vec<i32>>();
    ASSERT_EQ(first_two.len(), 2_usize);
    EXPECT_EQ(first_two[0_usize], 0_i32);
    EXPECT_EQ(first_two[1_usize], 1_i32);

    auto rest = it.collect<Vec<i32>>(); // continues from where by_ref left off
    ASSERT_EQ(rest.len(), 8_usize);
    EXPECT_EQ(rest[0_usize], 2_i32);
    EXPECT_EQ(rest[7_usize], 9_i32);
}

TEST(Iter, StringCharsBytes) {
    auto s   = String::make("abc"_str);
    auto cnt = s->chars().count();
    EXPECT_EQ(cnt, 3_usize);

    auto sum = s->bytes()
                   .map([](u8 b) {
                       return rstd::convert::into<i32>(b);
                   })
                   .sum();
    EXPECT_EQ(sum, 294_i32);

    auto text  = String::make("hi"_str);
    auto bytes = text->bytes().collect<Vec<u8>>();
    auto upper = String::from_utf8(rstd::move(bytes)).unwrap();
    EXPECT_EQ(upper.len(), 2_usize);
}

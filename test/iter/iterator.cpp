#include <gtest/gtest.h>
import rstd;

using namespace rstd::prelude;
using rstd::Option;
using rstd::Some;
using rstd::None;
using rstd::ref;
using rstd::vec::Vec;
using rstd::string::String;
namespace iter = rstd::iter;

TEST(Iter, RangeCollect) {
    auto v = iter::range(0, 5).collect<Vec<i32>>();
    ASSERT_EQ(v.len(), 5u);
    for (i32 i = 0; i < 5; ++i) EXPECT_EQ(v[i], i);
}

TEST(Iter, RangeSumCountFold) {
    EXPECT_EQ(iter::range(1, 5).sum(), 10);
    EXPECT_EQ(iter::range(0, 7).count(), 7u);
    auto s = iter::range(1, 5).fold(0, [](i32 a, i32 b) { return a + b; });
    EXPECT_EQ(s, 10);
}

TEST(Iter, MapFilterChain) {
    auto v = iter::range(0, 10)
                 .map([](i32 x) { return x * 2; })
                 .filter([](const i32& x) { return x > 4; })
                 .collect<Vec<i32>>();
    // 0..10 -> *2 -> 0,2,4,6,8,10,12,14,16,18 -> >4 -> 6,8,10,12,14,16,18
    ASSERT_EQ(v.len(), 7u);
    EXPECT_EQ(v[0], 6);
    EXPECT_EQ(v[6], 18);
}

TEST(Iter, TakeSkipStepBy) {
    auto a = iter::range(0, 100).take(3).collect<Vec<i32>>();
    ASSERT_EQ(a.len(), 3u);
    EXPECT_EQ(a[2], 2);

    auto b = iter::range(0, 5).skip(2).collect<Vec<i32>>();
    ASSERT_EQ(b.len(), 3u);
    EXPECT_EQ(b[0], 2);

    auto c = iter::range(0, 10).step_by(3).collect<Vec<i32>>();
    ASSERT_EQ(c.len(), 4u);  // 0,3,6,9
    EXPECT_EQ(c[1], 3);
}

TEST(Iter, EnumerateZipChain) {
    auto e = iter::range(10, 13).enumerate().collect<Vec<rstd::tuple<usize, i32>>>();
    ASSERT_EQ(e.len(), 3u);
    EXPECT_EQ(e[1].get<0>(), 1u);
    EXPECT_EQ(e[1].get<1>(), 11);

    auto z = iter::range(0, 3).zip(iter::range(100, 105)).collect<Vec<rstd::tuple<i32, i32>>>();
    ASSERT_EQ(z.len(), 3u);
    EXPECT_EQ(z[2].get<1>(), 102);

    auto c = iter::range(0, 2).chain(iter::range(10, 12)).collect<Vec<i32>>();
    ASSERT_EQ(c.len(), 4u);
    EXPECT_EQ(c[2], 10);
}

TEST(Iter, FindAnyAllPosition) {
    EXPECT_TRUE(iter::range(0, 5).any([](const i32& x) { return x == 3; }));
    EXPECT_FALSE(iter::range(0, 5).all([](const i32& x) { return x < 3; }));
    auto p = iter::range(0, 5).position([](const i32& x) { return x == 2; });
    EXPECT_EQ(p, Some(2u));
    auto f = iter::range(0, 5).find([](const i32& x) { return x > 2; });
    EXPECT_EQ(f, Some(3));
}

TEST(Iter, Sources) {
    auto o = iter::once(7).collect<Vec<i32>>();
    ASSERT_EQ(o.len(), 1u);
    EXPECT_EQ(o[0], 7);

    auto r = iter::repeat(9).take(3).collect<Vec<i32>>();
    ASSERT_EQ(r.len(), 3u);
    EXPECT_EQ(r[2], 9);

    auto em = iter::empty<i32>().collect<Vec<i32>>();
    EXPECT_EQ(em.len(), 0u);
}

TEST(Iter, VecIterCopied) {
    Vec<i32> v;
    v.push(1);
    v.push(2);
    v.push(3);
    auto doubled = v.iter().copied().map([](i32 x) { return x * 10; }).collect<Vec<i32>>();
    ASSERT_EQ(doubled.len(), 3u);
    EXPECT_EQ(doubled[0], 10);
    EXPECT_EQ(doubled[2], 30);

    // borrow sum
    i32 total = 0;
    v.iter().for_each([&](ref<i32> x) { total += *x; });
    EXPECT_EQ(total, 6);
}

TEST(Iter, VecIntoIter) {
    Vec<i32> v;
    v.push(5);
    v.push(6);
    v.push(7);
    auto out = v.into_iter().filter([](const i32& x) { return x != 6; }).collect<Vec<i32>>();
    ASSERT_EQ(out.len(), 2u);
    EXPECT_EQ(out[0], 5);
    EXPECT_EQ(out[1], 7);
}

TEST(Iter, RevCycle) {
    auto r = iter::range(0, 5).rev().collect<Vec<i32>>();
    ASSERT_EQ(r.len(), 5u);
    EXPECT_EQ(r[0], 4);
    EXPECT_EQ(r[4], 0);

    auto c = iter::range(0, 3).cycle().take(7).collect<Vec<i32>>();
    ASSERT_EQ(c.len(), 7u);
    EXPECT_EQ(c[3], 0);
    EXPECT_EQ(c[6], 0);
}

TEST(Iter, TakeWhileSkipWhile) {
    auto a = iter::range(0, 10).take_while([](const i32& x) { return x < 5; }).collect<Vec<i32>>();
    ASSERT_EQ(a.len(), 5u);
    EXPECT_EQ(a[4], 4);

    auto b = iter::range(0, 10).skip_while([](const i32& x) { return x < 5; }).collect<Vec<i32>>();
    ASSERT_EQ(b.len(), 5u);
    EXPECT_EQ(b[0], 5);
}

TEST(Iter, FilterMapMapWhile) {
    auto a = iter::range(0, 6)
                 .filter_map([](i32 x) -> Option<i32> {
                     if (x % 2 == 0) return Some(x * 10);
                     return None();
                 })
                 .collect<Vec<i32>>();
    ASSERT_EQ(a.len(), 3u);  // 0,20,40
    EXPECT_EQ(a[1], 20);

    auto b = iter::range(0, 10)
                 .map_while([](i32 x) -> Option<i32> {
                     if (x < 3) return Some(x);
                     return None();
                 })
                 .collect<Vec<i32>>();
    ASSERT_EQ(b.len(), 3u);
    EXPECT_EQ(b[2], 2);
}

TEST(Iter, ScanIntersperse) {
    auto a = iter::range(1, 5)
                 .scan(0, [](i32& st, i32 x) -> Option<i32> {
                     st += x;
                     return Some(st);
                 })
                 .collect<Vec<i32>>();
    ASSERT_EQ(a.len(), 4u);  // 1,3,6,10
    EXPECT_EQ(a[3], 10);

    auto b = iter::range(1, 4).intersperse(0).collect<Vec<i32>>();
    ASSERT_EQ(b.len(), 5u);  // 1,0,2,0,3
    EXPECT_EQ(b[1], 0);
    EXPECT_EQ(b[2], 2);
}

TEST(Iter, FlatMapFlatten) {
    auto a = iter::range(0, 4)
                 .flat_map([](i32 x) { return iter::range(0, x); })
                 .collect<Vec<i32>>();
    // 0:[],1:[0],2:[0,1],3:[0,1,2] -> 0,0,1,0,1,2
    ASSERT_EQ(a.len(), 6u);
    EXPECT_EQ(a[0], 0);
    EXPECT_EQ(a[5], 2);

    Vec<rstd::iter::Range<i32>> vr;
    vr.push(iter::range(0, 2));
    vr.push(iter::range(10, 12));
    auto b = vr.into_iter().flatten().collect<Vec<i32>>();
    ASSERT_EQ(b.len(), 4u);
    EXPECT_EQ(b[0], 0);
    EXPECT_EQ(b[2], 10);
}

TEST(Iter, PeekableInspectFuse) {
    auto p = iter::range(0, 3).peekable();
    ASSERT_NE(p.peek(), nullptr);
    EXPECT_EQ(*p.peek(), 0);
    EXPECT_EQ(p.next(), Some(0));
    EXPECT_EQ(p.next(), Some(1));

    i32  seen = 0;
    auto v    = iter::range(0, 3)
                 .inspect([&](const i32& x) { seen += x; })
                 .collect<Vec<i32>>();
    EXPECT_EQ(seen, 3);
    EXPECT_EQ(v.len(), 3u);
}

TEST(Iter, ReduceMinByKeyTryFoldEq) {
    EXPECT_EQ(iter::range(1, 5).reduce([](i32 a, i32 b) { return a + b; }), Some(10));

    auto m = iter::range(0, 5).min_by_key([](i32 x) { return (x - 2) * (x - 2); });
    EXPECT_EQ(m, Some(2));

    auto tf = iter::range(1, 5).try_fold(0, [](i32 a, i32 b) -> Option<i32> {
        return Some(a + b);
    });
    EXPECT_EQ(tf, Some(10));

    EXPECT_TRUE(iter::range(0, 3).eq(iter::range(0, 3)));
    EXPECT_FALSE(iter::range(0, 3).eq(iter::range(0, 4)));
    EXPECT_TRUE(iter::range(0, 3).lt(iter::range(0, 4)));
    EXPECT_TRUE(iter::range(0, 4).gt(iter::range(0, 3)));
    EXPECT_TRUE(iter::range(0, 3).ne(iter::range(1, 4)));
}

TEST(Iter, VecClonedDoubleEnded) {
    Vec<i32> v;
    v.push(1);
    v.push(2);
    v.push(3);
    auto cl = v.iter().cloned().collect<Vec<i32>>();
    ASSERT_EQ(cl.len(), 3u);
    EXPECT_EQ(cl[1], 2);

    // DoubleEndedIterator on SliceIter via trait
    auto it   = v.iter();
    auto back = rstd::as<iter::DoubleEndedIterator>(it).next_back();
    ASSERT_TRUE(back.is_some());
    EXPECT_EQ(**back, 3);
}

TEST(Iter, SliceArray) {
    int arr[4] = { 10, 20, 30, 40 };
    auto s     = iter::from_array(arr).copied().collect<Vec<i32>>();
    ASSERT_EQ(s.len(), 4u);
    EXPECT_EQ(s[2], 30);

    iter::from_array_mut(arr).for_each([](rstd::mut_ref<int> x) { *x += 1; });
    EXPECT_EQ(arr[0], 11);
    EXPECT_EQ(arr[3], 41);
}

TEST(Iter, MinByMaxBy) {
    auto cmp = [](const i32& a, const i32& b) { return a <=> b; };
    EXPECT_EQ(iter::range(0, 5).min_by(cmp), Some(0));
    EXPECT_EQ(iter::range(0, 5).max_by(cmp), Some(4));
}

TEST(Iter, RpositionNthBackAdvanceBy) {
    auto p = iter::range(0, 5).rposition([](const i32& x) { return x < 3; });
    EXPECT_EQ(p, Some(2u));  // last element < 3 is value 2 at index 2

    EXPECT_EQ(iter::range(0, 5).nth_back(0), Some(4));
    EXPECT_EQ(iter::range(0, 5).nth_back(1), Some(3));

    auto it = iter::range(0, 5);
    EXPECT_EQ(it.advance_by(2), 2u);
    EXPECT_EQ(it.next(), Some(2));
    EXPECT_EQ(it.advance_by(100), 2u);  // only 3,4 remain
}

TEST(Iter, IsSorted) {
    EXPECT_TRUE(iter::range(0, 5).is_sorted());

    Vec<i32> v;
    v.push(3);
    v.push(1);
    v.push(2);
    EXPECT_FALSE(v.iter().copied().is_sorted());

    EXPECT_TRUE(iter::range(0, 5).is_sorted_by_key([](i32 x) { return x; }));
    EXPECT_FALSE(iter::range(0, 5).is_sorted_by_key([](i32 x) { return -x; }));
}

TEST(Iter, PartitionUnzip) {
    auto pr = iter::range(0, 6).partition<Vec<i32>>([](const i32& x) { return x % 2 == 0; });
    ASSERT_EQ(pr.get<0>().len(), 3u);  // evens
    ASSERT_EQ(pr.get<1>().len(), 3u);  // odds
    EXPECT_EQ(pr.get<0>()[1], 2);
    EXPECT_EQ(pr.get<1>()[0], 1);

    auto uz = iter::range(0, 3)
                  .map([](i32 x) { return rstd::tuple<i32, i32>(x, x * x); })
                  .unzip<Vec<i32>, Vec<i32>>();
    ASSERT_EQ(uz.get<0>().len(), 3u);
    ASSERT_EQ(uz.get<1>().len(), 3u);
    EXPECT_EQ(uz.get<0>()[2], 2);
    EXPECT_EQ(uz.get<1>()[2], 4);
}

TEST(Iter, ByRef) {
    auto it        = iter::range(0, 10);
    auto first_two = it.by_ref().take(2).collect<Vec<i32>>();
    ASSERT_EQ(first_two.len(), 2u);
    EXPECT_EQ(first_two[0], 0);
    EXPECT_EQ(first_two[1], 1);

    auto rest = it.collect<Vec<i32>>();  // continues from where by_ref left off
    ASSERT_EQ(rest.len(), 8u);
    EXPECT_EQ(rest[0], 2);
    EXPECT_EQ(rest[7], 9);
}

TEST(Iter, StringCharsBytes) {
    auto s   = String::make("abc");
    auto cnt = s.chars().count();
    EXPECT_EQ(cnt, 3u);

    auto sum = s.bytes().map([](u8 b) { return static_cast<i32>(b); }).sum();
    EXPECT_EQ(sum, static_cast<i32>('a') + 'b' + 'c');

    // collect chars back into a String
    auto upper = String::make("hi").bytes().collect<String>();
    EXPECT_EQ(upper.len(), 2u);
}

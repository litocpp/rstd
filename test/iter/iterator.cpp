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
    auto cnt = s.chars().count();
    EXPECT_EQ(cnt, 3_usize);

    auto sum = s.bytes()
                   .map([](u8 b) {
                       return rstd::convert::into<i32>(b);
                   })
                   .sum();
    EXPECT_EQ(sum, 294_i32);

    auto bytes = String::make("hi"_str).bytes().collect<Vec<u8>>();
    auto upper = String::from_utf8(rstd::move(bytes)).unwrap();
    EXPECT_EQ(upper.len(), 2_usize);
}

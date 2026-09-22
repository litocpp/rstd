#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace slices = rstd::slice_;
namespace iter   = rstd::iter;

template<typename Make, typename Key, typename Check>
void check_sort_variants(Make make, Key key, Check check) {
    {
        auto values = make();
        slices::sort_by(values.as_mut_slice().as_mut_ref(), [&](const auto& a, const auto& b) {
            return key(a) <=> key(b);
        });
        check(values, true);
    }
    {
        auto values = make();
        slices::sort_by_key(values.as_mut_slice().as_mut_ref(), key);
        check(values, true);
    }
    {
        auto values = make();
        int  calls  = 0;
        slices::sort_by_cached_key(values.as_mut_slice().as_mut_ref(), [&](const auto& value) {
            ++calls;
            return key(value);
        });
        EXPECT_EQ(calls, int(values.len().to_primitive()));
        check(values, true);
    }
    {
        auto values = make();
        slices::sort_unstable_by(values.as_mut_slice().as_mut_ref(),
                                 [&](const auto& a, const auto& b) {
                                     return key(a) < key(b);
                                 });
        check(values, false);
    }
    {
        auto values = make();
        slices::sort_unstable_by_key(values.as_mut_slice().as_mut_ref(), key);
        check(values, false);
    }
}

template<typename Make>
void check_borrowed_sort(Make make, rstd::byte* data) {
    check_sort_variants(
        make,
        [](const auto& item) {
            return item.get();
        },
        [&](auto& values, bool stable) {
            const int original[]  = { 3, 1, 2, 1 };
            const int sorted[]    = { 1, 1, 2, 3 };
            const int positions[] = { 1, 3, 2, 0 };
            bool      seen[4]     = {};
            ASSERT_EQ(values.len(), 4_usize);
            for (usize i; i < values.len(); ++i) {
                EXPECT_EQ(data[i.to_primitive()], rstd::byte(original[i.to_primitive()]));
                EXPECT_EQ(values[i].get(), u8(sorted[i.to_primitive()]));
                auto position = values[i].as_raw_ptr() - data;
                ASSERT_GE(position, 0);
                ASSERT_LT(position, 4);
                EXPECT_FALSE(seen[position]);
                seen[position] = true;
                if (stable) EXPECT_EQ(position, positions[i.to_primitive()]);
            }
        });
}

TEST(SliceSort, BorrowedBytesPreserveSourceAndIdentity) {
    rstd::byte data[] = { rstd::byte { 3 }, rstd::byte { 1 }, rstd::byte { 2 }, rstd::byte { 1 } };
    check_borrowed_sort(
        [&] {
            return iter::into_iter(mut_ref<u8[]>::from_raw_parts(data, 4_usize))
                .collect<Vec<mut_ref<u8>>>();
        },
        data);
    check_borrowed_sort(
        [&] {
            return iter::from_slice(slice<u8>::from_raw_parts(data, 4_usize))
                .collect<Vec<ref<u8>>>();
        },
        data);
}

struct SortBorrow {
    mut_ref<u8> item;
    int*        drops;
    SortBorrow(mut_ref<u8> item, int& drops): item(item), drops(&drops) {}
    SortBorrow(SortBorrow&& other) noexcept
        : item(other.item), drops(rstd::exchange(other.drops, nullptr)) {}
    SortBorrow(const SortBorrow&)               = delete;
    auto operator=(SortBorrow&&) -> SortBorrow& = default;
    ~SortBorrow() {
        if (drops) ++*drops;
    }
};

TEST(SliceSort, NestedProxyAndMoveOnlyLifetime) {
    rstd::byte data[] = { rstd::byte { 3 }, rstd::byte { 1 }, rstd::byte { 2 } };
    int        drops  = 0;
    check_sort_variants(
        [&] {
            return iter::into_iter(mut_ref<u8[]>::from_raw_parts(data, 3_usize))
                .map([&](mut_ref<u8> item) {
                    return SortBorrow(item, drops);
                })
                .collect<Vec<SortBorrow>>();
        },
        [](const SortBorrow& row) {
            return row.item.get();
        },
        [&](auto& values, bool) {
            EXPECT_EQ(values[0_usize].item.as_raw_ptr(), data + 1);
            EXPECT_EQ(values[1_usize].item.as_raw_ptr(), data + 2);
            EXPECT_EQ(values[2_usize].item.as_raw_ptr(), data);
            EXPECT_EQ(data[0], rstd::byte { 3 });
            EXPECT_EQ(data[1], rstd::byte { 1 });
            EXPECT_EQ(data[2], rstd::byte { 2 });
        });
    EXPECT_EQ(drops, 15);
}

constexpr bool unstable_proxy_sort() {
    rstd::byte  data[]    = { rstd::byte { 3 }, rstd::byte { 1 }, rstd::byte { 2 } };
    mut_ref<u8> handles[] = { mut_ref<u8>::from_raw_parts(data),
                              mut_ref<u8>::from_raw_parts(data + 1),
                              mut_ref<u8>::from_raw_parts(data + 2) };
    slices::sort_unstable_by_key(mut_ref<mut_ref<u8>[]>::from_raw_parts(handles, 3_usize),
                                 [](const mut_ref<u8>& item) {
                                     return item.get();
                                 });
    return handles[0].as_raw_ptr() == data + 1 && handles[1].as_raw_ptr() == data + 2 &&
           handles[2].as_raw_ptr() == data && data[0] == rstd::byte { 3 } &&
           data[1] == rstd::byte { 1 } && data[2] == rstd::byte { 2 };
}
static_assert(unstable_proxy_sort());

TEST(SliceSort, ConstexprProxySorting) {
    EXPECT_TRUE(unstable_proxy_sort());
}

struct SortRow {
    i32 key;
    i32 ordinal;
    SortRow(i32 key, i32 ordinal): key(key), ordinal(ordinal) {}
    SortRow(SortRow&&)                    = default;
    auto operator=(SortRow&&) -> SortRow& = delete;
    SortRow(const SortRow&)               = delete;
};

TEST(SliceSort, StableMoveOnlyAndCachedKeys) {
    auto values = iter::range(0_i32, 20_i32)
                      .map([](i32 x) {
                          return SortRow(x % 3_i32, x);
                      })
                      .collect<Vec<SortRow>>();
    int  calls  = 0;
    slices::sort_by_cached_key(values.as_mut_slice().as_mut_ref(), [&](const SortRow& row) {
        ++calls;
        return row.key;
    });
    EXPECT_EQ(calls, 20);
    for (usize i(1); i < values.len(); ++i) {
        const auto& a = values[i - 1_usize];
        const auto& b = values[i];
        EXPECT_TRUE(a.key < b.key || (a.key == b.key && a.ordinal < b.ordinal));
    }
}

TEST(SliceSort, MultiKeyAndTies) {
    const i32 keys[]   = { 2_i32, 1_i32, 1_i32, 1_i32 };
    const i32 scores[] = { 90_i32, 80_i32, 90_i32, 90_i32 };
    auto      indices  = iter::range(0_usize, 4_usize).collect<Vec<usize>>();
    slices::sort_by(indices.as_mut_slice().as_mut_ref(), [&](usize a, usize b) {
        auto order = keys[a.to_primitive()] <=> keys[b.to_primitive()];
        return order == rstd::strong_ordering::equal
                   ? scores[b.to_primitive()] <=> scores[a.to_primitive()]
                   : order;
    });
    EXPECT_EQ(indices[0_usize], 2_usize);
    EXPECT_EQ(indices[1_usize], 3_usize);
    EXPECT_EQ(indices[2_usize], 1_usize);
    EXPECT_EQ(indices[3_usize], 0_usize);
}

TEST(SliceSort, PermutationsKeepEveryValue) {
    for (int seed = 0; seed < 50; ++seed) {
        auto values = iter::range(0_i32, 31_i32)
                          .map([seed](i32 x) {
                              return (x * 7_i32 + i32(seed)) % 31_i32;
                          })
                          .collect<Vec<i32>>();
        slices::sort_by_key(values.as_mut_slice().as_mut_ref(), [](i32 x) {
            return x;
        });
        for (usize i; i < values.len(); ++i) EXPECT_EQ(values[i], i32(i.to_primitive()));
    }
}

template<typename T>
void check_empty_and_singleton() {
    Vec<T> values;
    int    calls = 0;
    auto   key   = [&](T x) {
        ++calls;
        return x;
    };
    slices::sort_by_cached_key(values.as_mut_slice().as_mut_ref(), key);
    values.push(T(1));
    slices::sort_by_cached_key(values.as_mut_slice().as_mut_ref(), key);
    EXPECT_EQ(calls, 0);
    values.push(T(0));
    slices::sort_unstable_by_key(values.as_mut_slice().as_mut_ref(), key);
    EXPECT_EQ(T(values[0_usize]), T(0));
    EXPECT_EQ(T(values[1_usize]), T(1));
}

TEST(SliceSort, EmptySingletonAndUnstableKeys) {
    check_empty_and_singleton<i32>();
    check_empty_and_singleton<u8>();
}

TEST(SliceSort, ByteStableKeys) {
    auto values = iter::range(u8(1), u8(10)).collect<Vec<u8>>();
    int  calls  = 0;
    slices::sort_by_cached_key(values.as_mut_slice().as_mut_ref(), [&](u8 value) {
        ++calls;
        return value % u8(2);
    });
    EXPECT_EQ(calls, 9);
    const int expected[] = { 2, 4, 6, 8, 1, 3, 5, 7, 9 };
    for (usize i; i < values.len(); ++i) EXPECT_EQ(u8(values[i]), u8(expected[i.to_primitive()]));
    slices::sort_by(values.as_mut_slice().as_mut_ref(), [](u8 a, u8 b) {
        return b <=> a;
    });
    for (usize i; i < values.len(); ++i) EXPECT_EQ(u8(values[i]), u8(9 - i.to_primitive()));
    slices::sort_by_key(values.as_mut_slice().as_mut_ref(), [](u8 value) {
        return value;
    });
    for (usize i; i < values.len(); ++i) EXPECT_EQ(u8(values[i]), u8(i.to_primitive() + 1));
}

TEST(SliceSort, ByteKeysForObjectElements) {
    auto values = iter::range(1_i32, 6_i32).collect<Vec<i32>>();
    slices::sort_by_cached_key(values.as_mut_slice().as_mut_ref(), [](i32 value) {
        return u8((value % 2_i32).to_primitive());
    });
    const int expected[] = { 2, 4, 1, 3, 5 };
    for (usize i; i < values.len(); ++i) EXPECT_EQ(values[i], i32(expected[i.to_primitive()]));
}

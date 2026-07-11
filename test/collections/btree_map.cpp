#include <gtest/gtest.h>
import rstd;

using namespace rstd::prelude;
using rstd::collections::BTreeMap;
namespace iter = rstd::iter;

namespace
{

struct TrackedValue {
    static inline i32 live = 0;
    i32               value;

    explicit TrackedValue(i32 v): value(v) { ++live; }
    TrackedValue(const TrackedValue&)            = delete;
    TrackedValue& operator=(const TrackedValue&) = delete;
    TrackedValue(TrackedValue&& other) noexcept: value(other.value) { ++live; }
    TrackedValue& operator=(TrackedValue&& other) noexcept {
        value = other.value;
        return *this;
    }
    ~TrackedValue() { --live; }
};

struct MoveOnlyKey {
    i32 value;

    explicit MoveOnlyKey(i32 v): value(v) {}
    MoveOnlyKey(const MoveOnlyKey&)                = delete;
    MoveOnlyKey& operator=(const MoveOnlyKey&)     = delete;
    MoveOnlyKey(MoveOnlyKey&&) noexcept            = default;
    MoveOnlyKey& operator=(MoveOnlyKey&&) noexcept = default;

    friend bool operator<(const MoveOnlyKey& left, const MoveOnlyKey& right) {
        return left.value < right.value;
    }
};

} // namespace

TEST(BTreeMap, BasicLookupAndReplacement) {
    auto map = BTreeMap<i32, i32>::make();
    EXPECT_TRUE(map.is_empty());
    EXPECT_TRUE(map.insert(2, 20).is_none());
    EXPECT_TRUE(map.insert(1, 10).is_none());
    EXPECT_TRUE(map.insert(3, 30).is_none());
    EXPECT_EQ(map.insert(2, 200), Some(20));
    EXPECT_EQ(map.len(), 3u);
    EXPECT_TRUE(map.contains_key(1));
    EXPECT_FALSE(map.contains_key(4));

    auto value = map.get(2);
    ASSERT_TRUE(value.is_some());
    EXPECT_EQ(**value, 200);

    auto pair = map.get_key_value(3);
    ASSERT_TRUE(pair.is_some());
    EXPECT_EQ(*pair->get<0>(), 3);
    EXPECT_EQ(*pair->get<1>(), 30);

    auto first = map.first_key_value();
    auto last  = map.last_key_value();
    ASSERT_TRUE(first.is_some());
    ASSERT_TRUE(last.is_some());
    EXPECT_EQ(*first->get<0>(), 1);
    EXPECT_EQ(*last->get<0>(), 3);

    map.clear();
    EXPECT_TRUE(map.is_empty());
    EXPECT_TRUE(map.get(2).is_none());
}

TEST(BTreeMap, SplitsAndIteratesInOrder) {
    auto map = BTreeMap<i32, i32>::make();
    for (i32 i = 0; i < 2000; ++i) {
        i32 key = (i * 37) % 2000;
        EXPECT_TRUE(map.insert(key, key * 10).is_none());
    }
    ASSERT_EQ(map.len(), 2000u);

    auto iter = map.iter();
    for (i32 expected = 0; expected < 2000; ++expected) {
        auto item = iter.next();
        ASSERT_TRUE(item.is_some());
        EXPECT_EQ(*item->get<0>(), expected);
        EXPECT_EQ(*item->get<1>(), expected * 10);
    }
    EXPECT_TRUE(iter.next().is_none());

    auto mixed = map.iter();
    for (i32 expected = 0; expected < 1000; ++expected) {
        auto front = mixed.next();
        auto back  = mixed.next_back();
        ASSERT_TRUE(front.is_some());
        ASSERT_TRUE(back.is_some());
        EXPECT_EQ(*front->get<0>(), expected);
        EXPECT_EQ(*back->get<0>(), 1999 - expected);
    }
    EXPECT_TRUE(mixed.next().is_none());
    EXPECT_EQ(mixed.len(), 0u);
}

TEST(BTreeMap, RebalancesDuringInterleavedRemoval) {
    auto map = BTreeMap<i32, i32>::make();
    for (i32 i = 0; i < 1500; ++i) map.insert((i * 43) % 1500, i);

    for (i32 key = 0; key < 1500; key += 2) {
        auto removed = map.remove(key);
        ASSERT_TRUE(removed.is_some());
        EXPECT_FALSE(map.contains_key(key));
    }
    EXPECT_EQ(map.len(), 750u);

    i32  expected = 1;
    auto keys     = map.keys();
    for (auto item = keys.next(); item.is_some(); item = keys.next()) {
        EXPECT_EQ(**item, expected);
        expected += 2;
    }

    for (i32 key = 1499; key >= 1; key -= 2) {
        auto removed = map.remove_entry(key);
        ASSERT_TRUE(removed.is_some());
        EXPECT_EQ(removed->get<0>(), key);
    }
    EXPECT_TRUE(map.is_empty());
    EXPECT_TRUE(map.remove(1).is_none());
}

TEST(BTreeMap, MutableAndOwningIterators) {
    auto map = BTreeMap<i32, i32>::make();
    for (i32 i = 0; i < 64; ++i) map.insert(i, i);

    auto value = map.get_mut(10);
    ASSERT_TRUE(value.is_some());
    **value = 1000;

    auto values = map.values_mut();
    for (auto item = values.next(); item.is_some(); item = values.next()) **item += 1;
    EXPECT_EQ(**map.get(10), 1001);

    auto owned = map.into_iter();
    EXPECT_TRUE(map.is_empty());
    for (i32 expected = 0; expected < 64; ++expected) {
        auto item = owned.next();
        ASSERT_TRUE(item.is_some());
        EXPECT_EQ(item->get<0>(), expected);
        EXPECT_EQ(item->get<1>(), expected == 10 ? 1001 : expected + 1);
    }
    EXPECT_TRUE(owned.next().is_none());
}

TEST(BTreeMap, PopAndCollect) {
    auto map = iter::range(0, 100)
                   .map([](i32 key) {
                       return rstd::tuple<i32, i32>(key, key * key);
                   })
                   .collect<BTreeMap<i32, i32>>();

    for (i32 low = 0, high = 99; low <= high; ++low, --high) {
        auto first = map.pop_first();
        ASSERT_TRUE(first.is_some());
        EXPECT_EQ(first->get<0>(), low);
        if (low == high) break;
        auto last = map.pop_last();
        ASSERT_TRUE(last.is_some());
        EXPECT_EQ(last->get<0>(), high);
    }
    EXPECT_TRUE(map.is_empty());
    EXPECT_TRUE(map.pop_first().is_none());
    EXPECT_TRUE(map.pop_last().is_none());
}

TEST(BTreeMap, MoveOnlyValuesHaveBalancedLifetimes) {
    EXPECT_EQ(TrackedValue::live, 0);
    {
        auto map = BTreeMap<i32, TrackedValue>::make();
        for (i32 i = 0; i < 256; ++i) map.insert(i, TrackedValue(i));

        auto old = map.insert(7, TrackedValue(700));
        ASSERT_TRUE(old.is_some());
        EXPECT_EQ(old->value, 7);

        auto removed = map.remove(9);
        ASSERT_TRUE(removed.is_some());
        EXPECT_EQ(removed->value, 9);

        auto value = map.get(7);
        ASSERT_TRUE(value.is_some());
        EXPECT_EQ((**value).value, 700);
        map.clear();
    }
    EXPECT_EQ(TrackedValue::live, 0);
}

TEST(BTreeMap, RandomizedOperationsMatchOrderedModel) {
    constexpr usize key_count = 257;
    bool            present[key_count] {};
    i32             values[key_count] {};
    usize           expected_len = 0;
    u32             state        = 0x12345678u;
    auto            map          = BTreeMap<i32, i32>::make();

    for (i32 operation = 0; operation < 10000; ++operation) {
        state   = state * 1664525u + 1013904223u;
        i32 key = static_cast<i32>((state >> 8) % key_count);
        if (state % 3 == 0) {
            i32  value = operation * 3;
            auto old   = map.insert(key, value);
            if (present[key]) {
                ASSERT_TRUE(old.is_some());
                EXPECT_EQ(*old, values[key]);
            } else {
                EXPECT_TRUE(old.is_none());
                present[key] = true;
                ++expected_len;
            }
            values[key] = value;
        } else if (state % 3 == 1) {
            auto removed = map.remove(key);
            if (present[key]) {
                ASSERT_TRUE(removed.is_some());
                EXPECT_EQ(*removed, values[key]);
                present[key] = false;
                --expected_len;
            } else {
                EXPECT_TRUE(removed.is_none());
            }
        } else {
            auto value = map.get(key);
            EXPECT_EQ(value.is_some(), present[key]);
            if (present[key]) EXPECT_EQ(**value, values[key]);
        }

        if (operation % 37 == 0) {
            EXPECT_EQ(map.len(), expected_len);
            auto iter = map.iter();
            for (usize index = 0; index < key_count; ++index) {
                if (! present[index]) continue;
                auto item = iter.next();
                ASSERT_TRUE(item.is_some());
                EXPECT_EQ(*item->get<0>(), static_cast<i32>(index));
                EXPECT_EQ(*item->get<1>(), values[index]);
            }
            EXPECT_TRUE(iter.next().is_none());
        }
    }
}

TEST(BTreeMap, MoveOnlyKeysAndMapMovesRemainValid) {
    auto map = BTreeMap<MoveOnlyKey, i32>::make();
    for (i32 i = 99; i >= 0; --i) map.insert(MoveOnlyKey(i), i * 2);

    MoveOnlyKey lookup(40);
    ASSERT_TRUE(map.get(lookup).is_some());
    EXPECT_EQ(**map.get(lookup), 80);
    EXPECT_EQ(map.remove(lookup), Some(80));

    auto moved = rstd::move(map);
    EXPECT_TRUE(map.is_empty());
    EXPECT_EQ(moved.len(), 99u);

    auto destination = BTreeMap<MoveOnlyKey, i32>::make();
    destination.insert(MoveOnlyKey(200), 1);
    destination = rstd::move(moved);
    EXPECT_TRUE(moved.is_empty());
    EXPECT_EQ(destination.len(), 99u);

    auto empty  = BTreeMap<MoveOnlyKey, i32>::make();
    destination = rstd::move(empty);
    EXPECT_TRUE(destination.is_empty());
    EXPECT_TRUE(empty.is_empty());
}

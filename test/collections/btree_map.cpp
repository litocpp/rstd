#include <gtest/gtest.h>
import rstd;

using namespace rstd::prelude;
using rstd::collections::BTreeMap;
using rstd::string::String;
namespace iter = rstd::iter;

namespace
{

struct TrackedValue {
    static inline int live = 0;
    int               value;

    explicit TrackedValue(int v): value(v) { ++live; }
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
    int value;

    explicit MoveOnlyKey(int v): value(v) {}
    MoveOnlyKey(const MoveOnlyKey&)                = delete;
    MoveOnlyKey& operator=(const MoveOnlyKey&)     = delete;
    MoveOnlyKey(MoveOnlyKey&&) noexcept            = default;
    MoveOnlyKey& operator=(MoveOnlyKey&&) noexcept = default;

    friend bool operator<(const MoveOnlyKey& left, const MoveOnlyKey& right) {
        return left.value < right.value;
    }
};

template<typename T>
concept ConcreteCloneable = requires(const T& value) { value.clone(); };

static_assert(! ConcreteCloneable<BTreeMap<MoveOnlyKey, i32>>);

} // namespace

TEST(BTreeMap, BasicLookupAndReplacement) {
    auto map = BTreeMap<i32, i32>::make();
    EXPECT_TRUE(map.is_empty());
    EXPECT_TRUE(map.insert(i32(2), i32(20)).is_none());
    EXPECT_TRUE(map.insert(i32(1), i32(10)).is_none());
    EXPECT_TRUE(map.insert(i32(3), i32(30)).is_none());
    EXPECT_EQ(map.insert(i32(2), i32(200)), Some(i32(20)));
    EXPECT_EQ(map.len(), usize(3));
    EXPECT_TRUE(map.contains_key(i32(1)));
    EXPECT_FALSE(map.contains_key(i32(4)));

    auto value = map.get(i32(2));
    ASSERT_TRUE(value.is_some());
    EXPECT_EQ(**value, i32(200));

    auto pair = map.get_key_value(i32(3));
    ASSERT_TRUE(pair.is_some());
    EXPECT_EQ(*pair->template get<0>(), i32(3));
    EXPECT_EQ(*pair->template get<1>(), i32(30));

    auto first = map.first_key_value();
    auto last  = map.last_key_value();
    ASSERT_TRUE(first.is_some());
    ASSERT_TRUE(last.is_some());
    EXPECT_EQ(*first->template get<0>(), i32(1));
    EXPECT_EQ(*last->template get<0>(), i32(3));

    map.clear();
    EXPECT_TRUE(map.is_empty());
    EXPECT_TRUE(map.get(i32(2)).is_none());
}

TEST(BTreeMap, SplitsAndIteratesInOrder) {
    auto map = BTreeMap<i32, i32>::make();
    for (i32 i {}; i < i32(2000); i += i32(1)) {
        i32 key = (i * i32(37)) % i32(2000);
        EXPECT_TRUE(map.insert(key, key * i32(10)).is_none());
    }
    ASSERT_EQ(map.len(), usize(2000));

    auto iter = map.iter();
    for (i32 expected {}; expected < i32(2000); expected += i32(1)) {
        auto item = iter.next();
        ASSERT_TRUE(item.is_some());
        EXPECT_EQ(*item->template get<0>(), expected);
        EXPECT_EQ(*item->template get<1>(), expected * i32(10));
    }
    EXPECT_TRUE(iter.next().is_none());

    auto mixed = map.iter();
    for (i32 expected {}; expected < i32(1000); expected += i32(1)) {
        auto front = mixed.next();
        auto back  = mixed.next_back();
        ASSERT_TRUE(front.is_some());
        ASSERT_TRUE(back.is_some());
        EXPECT_EQ(*front->template get<0>(), expected);
        EXPECT_EQ(*back->template get<0>(), i32(1999) - expected);
    }
    EXPECT_TRUE(mixed.next().is_none());
    EXPECT_EQ(mixed.len(), usize());
}

TEST(BTreeMap, RebalancesDuringInterleavedRemoval) {
    auto map = BTreeMap<i32, i32>::make();
    for (i32 i {}; i < i32(1500); i += i32(1)) {
        map.insert((i * i32(43)) % i32(1500), i);
    }

    for (i32 key {}; key < i32(1500); key += i32(2)) {
        auto removed = map.remove(key);
        ASSERT_TRUE(removed.is_some());
        EXPECT_FALSE(map.contains_key(key));
    }
    EXPECT_EQ(map.len(), usize(750));

    i32  expected = i32(1);
    auto keys     = map.keys();
    for (auto item = keys.next(); item.is_some(); item = keys.next()) {
        EXPECT_EQ(**item, expected);
        expected += i32(2);
    }

    for (i32 key = i32(1499); key >= i32(1); key -= i32(2)) {
        auto removed = map.remove_entry(key);
        ASSERT_TRUE(removed.is_some());
        EXPECT_EQ(removed->template get<0>(), key);
    }
    EXPECT_TRUE(map.is_empty());
    EXPECT_TRUE(map.remove(i32(1)).is_none());
}

TEST(BTreeMap, MutableAndOwningIterators) {
    auto map = BTreeMap<i32, i32>::make();
    for (i32 i {}; i < i32(64); i += i32(1)) map.insert(i, i);

    auto value = map.get_mut(i32(10));
    ASSERT_TRUE(value.is_some());
    **value = i32(1000);

    auto values = map.values_mut();
    for (auto item = values.next(); item.is_some(); item = values.next()) **item += i32(1);
    EXPECT_EQ(**map.get(i32(10)), i32(1001));

    auto owned = map.into_iter();
    EXPECT_TRUE(map.is_empty());
    for (i32 expected {}; expected < i32(64); expected += i32(1)) {
        auto item = owned.next();
        ASSERT_TRUE(item.is_some());
        EXPECT_EQ(item->template get<0>(), expected);
        EXPECT_EQ(item->template get<1>(), expected == i32(10) ? i32(1001) : expected + i32(1));
    }
    EXPECT_TRUE(owned.next().is_none());
}

TEST(BTreeMap, IteratorItemsSupportStructuredBindings) {
    auto map = BTreeMap<i32, TrackedValue>::make();
    map.insert(i32(2), TrackedValue(20));

    {
        auto item = map.iter().next();
        ASSERT_TRUE(item.is_some());
        auto [key, value] = item.unwrap();
        EXPECT_EQ(*key, i32(2));
        EXPECT_EQ(value->value, 20);
    }

    {
        auto item = map.iter_mut().next();
        ASSERT_TRUE(item.is_some());
        auto [key, value] = item.unwrap();
        EXPECT_EQ(*key, i32(2));
        value->value = 21;
    }

    {
        const auto& const_map = map;
        auto        item      = const_map.iter().next();
        ASSERT_TRUE(item.is_some());
        const auto [key, value] = item.unwrap();
        EXPECT_EQ(*key, i32(2));
        EXPECT_EQ(value->value, 21);
    }

    auto item = map.into_iter().next();
    ASSERT_TRUE(item.is_some());
    auto [key, value] = item.unwrap();
    EXPECT_EQ(key, i32(2));
    EXPECT_EQ(value.value, 21);
}

TEST(BTreeMap, PopAndCollect) {
    auto map = iter::range(i32(), i32(100))
                   .map([](i32 key) {
                       return rstd::tuple<i32, i32>(key, key * key);
                   })
                   .collect<BTreeMap<i32, i32>>();

    for (i32 low {}, high = i32(99); low <= high; low += i32(1), high -= i32(1)) {
        auto first = map.pop_first();
        ASSERT_TRUE(first.is_some());
        EXPECT_EQ(first->template get<0>(), low);
        if (low == high) break;
        auto last = map.pop_last();
        ASSERT_TRUE(last.is_some());
        EXPECT_EQ(last->template get<0>(), high);
    }
    EXPECT_TRUE(map.is_empty());
    EXPECT_TRUE(map.pop_first().is_none());
    EXPECT_TRUE(map.pop_last().is_none());
}

TEST(BTreeMap, MoveOnlyValuesHaveBalancedLifetimes) {
    EXPECT_EQ(TrackedValue::live, 0);
    {
        auto map = BTreeMap<i32, TrackedValue>::make();
        for (i32 i {}; i < i32(256); i += i32(1)) {
            map.insert(i, TrackedValue(i.to_primitive()));
        }

        auto old = map.insert(i32(7), TrackedValue(700));
        ASSERT_TRUE(old.is_some());
        EXPECT_EQ(old->value, 7);

        auto removed = map.remove(i32(9));
        ASSERT_TRUE(removed.is_some());
        EXPECT_EQ(removed->value, 9);

        auto value = map.get(i32(7));
        ASSERT_TRUE(value.is_some());
        EXPECT_EQ((**value).value, 700);
        map.clear();
    }
    EXPECT_EQ(TrackedValue::live, 0);
}

TEST(BTreeMap, RandomizedOperationsMatchOrderedModel) {
    constexpr rstd::size_t key_count = 257;
    bool                   present[key_count] {};
    i32                    values[key_count] {};
    usize                  expected_len {};
    rstd::uint32_t         state = 0x12345678u;
    auto                   map   = BTreeMap<i32, i32>::make();

    for (int operation = 0; operation < 10000; ++operation) {
        state = state * 1664525u + 1013904223u;
        i32  key(static_cast<rstd::int32_t>((state >> 8) % key_count));
        auto model_index = static_cast<rstd::size_t>(key.to_primitive());
        if (state % 3 == 0) {
            i32  value(operation * 3);
            auto old = map.insert(key, value);
            if (present[model_index]) {
                ASSERT_TRUE(old.is_some());
                EXPECT_EQ(*old, values[model_index]);
            } else {
                EXPECT_TRUE(old.is_none());
                present[model_index] = true;
                ++expected_len;
            }
            values[model_index] = value;
        } else if (state % 3 == 1) {
            auto removed = map.remove(key);
            if (present[model_index]) {
                ASSERT_TRUE(removed.is_some());
                EXPECT_EQ(*removed, values[model_index]);
                present[model_index] = false;
                --expected_len;
            } else {
                EXPECT_TRUE(removed.is_none());
            }
        } else {
            auto value = map.get(key);
            EXPECT_EQ(value.is_some(), present[model_index]);
            if (present[model_index]) EXPECT_EQ(**value, values[model_index]);
        }

        if (operation % 37 == 0) {
            EXPECT_EQ(map.len(), expected_len);
            auto iter = map.iter();
            for (rstd::size_t index = 0; index < key_count; ++index) {
                if (! present[index]) continue;
                auto item = iter.next();
                ASSERT_TRUE(item.is_some());
                EXPECT_EQ(*item->template get<0>(), i32(static_cast<rstd::int32_t>(index)));
                EXPECT_EQ(*item->template get<1>(), values[index]);
            }
            EXPECT_TRUE(iter.next().is_none());
        }
    }
}

TEST(BTreeMap, MoveOnlyKeysAndMapMovesRemainValid) {
    auto map = BTreeMap<MoveOnlyKey, i32>::make();
    for (int i = 99; i >= 0; --i) map.insert(MoveOnlyKey(i), i32(i * 2));

    MoveOnlyKey lookup(40);
    ASSERT_TRUE(map.get(lookup).is_some());
    EXPECT_EQ(**map.get(lookup), i32(80));
    EXPECT_EQ(map.remove(lookup), Some(i32(80)));

    auto moved = rstd::move(map);
    EXPECT_TRUE(map.is_empty());
    EXPECT_EQ(moved.len(), usize(99));

    auto destination = BTreeMap<MoveOnlyKey, i32>::make();
    destination.insert(MoveOnlyKey(200), i32(1));
    destination = rstd::move(moved);
    EXPECT_TRUE(moved.is_empty());
    EXPECT_EQ(destination.len(), usize(99));

    auto empty  = BTreeMap<MoveOnlyKey, i32>::make();
    destination = rstd::move(empty);
    EXPECT_TRUE(destination.is_empty());
    EXPECT_TRUE(empty.is_empty());
}

TEST(BTreeMap, BorrowedStringLookupAndRemoval) {
    auto map = BTreeMap<String, i32>::make();
    map.insert(String::make("alpha"), i32(1));
    map.insert(String::make("beta"), i32(2));

    auto alpha = ref<rstd::str>("alpha");
    ASSERT_TRUE(map.contains_key(alpha));
    ASSERT_TRUE(map.get(alpha).is_some());
    EXPECT_EQ(**map.get(alpha), i32(1));

    auto beta = ref<rstd::str>("beta");
    ASSERT_TRUE(map.get_mut(beta).is_some());
    **map.get_mut(beta) = i32(20);
    EXPECT_EQ(map.remove(beta), Some(i32(20)));
    EXPECT_FALSE(map.contains_key(beta));
}

TEST(BTreeMap, CloneAndEqualityUseOwnedEntries) {
    auto map = BTreeMap<String, String>::make();
    map.insert(String::make("a"), String::make("one"));
    map.insert(String::make("b"), String::make("two"));

    auto direct   = map.clone();
    auto abstract = rstd::as<rstd::clone::Clone>(map).clone();
    EXPECT_EQ(map, direct);
    EXPECT_EQ(map, abstract);

    **direct.get_mut(ref<rstd::str>("a")) = String::make("changed");
    EXPECT_NE(map, direct);
    EXPECT_EQ(**map.get(ref<rstd::str>("a")), "one");
    EXPECT_EQ(**direct.get(ref<rstd::str>("a")), "changed");
    EXPECT_EQ(**abstract.get(ref<rstd::str>("a")), "one");
}

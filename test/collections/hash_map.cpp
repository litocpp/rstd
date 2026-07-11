#include <gtest/gtest.h>
import rstd;

using namespace rstd::prelude;
using rstd::collections::HashMap;
namespace iter = rstd::iter;

namespace
{

struct ConstantHasher {
    template<typename T>
    auto operator()(const T&) const noexcept -> u64 {
        return 7;
    }
};

struct TrackedHashValue {
    static inline i32 live = 0;
    i32               value;

    explicit TrackedHashValue(i32 v): value(v) { ++live; }
    TrackedHashValue(const TrackedHashValue&)            = delete;
    TrackedHashValue& operator=(const TrackedHashValue&) = delete;
    TrackedHashValue(TrackedHashValue&& other) noexcept: value(other.value) { ++live; }
    TrackedHashValue& operator=(TrackedHashValue&& other) noexcept {
        value = other.value;
        return *this;
    }
    ~TrackedHashValue() { --live; }
};

struct HashKey {
    i32 id;
    i32 identity;

    HashKey(i32 key_id, i32 key_identity): id(key_id), identity(key_identity) {}
    HashKey(const HashKey&)                = delete;
    HashKey& operator=(const HashKey&)     = delete;
    HashKey(HashKey&&) noexcept            = default;
    HashKey& operator=(HashKey&&) noexcept = default;

    friend bool operator==(const HashKey& left, const HashKey& right) {
        return left.id == right.id;
    }
};

} // namespace

namespace rstd
{

template<>
struct Impl<hash::Hash, HashKey> : ImplBase<HashKey> {
    void hash(hash::DefaultHasher& state) const noexcept { state.write_value(this->self().id); }
};

} // namespace rstd

TEST(HashMap, BasicLookupReplacementAndCapacity) {
    auto map = HashMap<i32, i32>::make();
    EXPECT_EQ(map.capacity(), 0u);
    EXPECT_TRUE(map.insert(1, 10).is_none());
    EXPECT_TRUE(map.insert(2, 20).is_none());
    EXPECT_EQ(map.insert(1, 100), Some(10));
    EXPECT_EQ(map.len(), 2u);
    EXPECT_TRUE(map.contains_key(1));
    EXPECT_FALSE(map.contains_key(3));
    EXPECT_EQ(**map.get(1), 100);

    auto pair = map.get_key_value(2);
    ASSERT_TRUE(pair.is_some());
    EXPECT_EQ(*pair->get<0>(), 2);
    EXPECT_EQ(*pair->get<1>(), 20);

    auto value = map.get_mut(2);
    ASSERT_TRUE(value.is_some());
    **value = 200;
    EXPECT_EQ(**map.get(2), 200);

    map.reserve(100);
    EXPECT_GE(map.capacity(), 102u);
    map.clear();
    EXPECT_TRUE(map.is_empty());
    EXPECT_GE(map.capacity(), 100u);
    map.shrink_to_fit();
    EXPECT_EQ(map.capacity(), 0u);
}

TEST(HashMap, CollisionChainsSurviveTombstonesAndRehash) {
    auto map = HashMap<i32, i32, ConstantHasher>::with_capacity(1);
    for (i32 i = 0; i < 300; ++i) map.insert(i, i * 2);
    for (i32 i = 0; i < 150; ++i) EXPECT_EQ(map.remove(i), Some(i * 2));
    for (i32 i = 150; i < 300; ++i) EXPECT_EQ(**map.get(i), i * 2);

    for (i32 i = 300; i < 500; ++i) map.insert(i, i * 2);
    for (i32 i = 150; i < 500; ++i) EXPECT_EQ(**map.get(i), i * 2);
    EXPECT_EQ(map.len(), 350u);

    map.retain([](const i32& key, i32& value) {
        value += 1;
        return key % 2 == 0;
    });
    for (i32 i = 150; i < 500; ++i) {
        auto value = map.get(i);
        EXPECT_EQ(value.is_some(), i % 2 == 0);
        if (value.is_some()) EXPECT_EQ(**value, i * 2 + 1);
    }
}

TEST(HashMap, IteratorsAndCollectPreserveAllEntries) {
    auto map = iter::range(0, 256)
                   .map([](i32 key) {
                       return rstd::tuple<i32, i32>(key, key + 1);
                   })
                   .collect<HashMap<i32, i32>>();

    bool seen[256] {};
    auto entries = map.iter_mut();
    for (auto item = entries.next(); item.is_some(); item = entries.next()) {
        i32 key = *item->get<0>();
        EXPECT_FALSE(seen[key]);
        seen[key] = true;
        *item->get<1>() += 10;
    }
    for (i32 i = 0; i < 256; ++i) {
        EXPECT_TRUE(seen[i]);
        EXPECT_EQ(**map.get(i), i + 11);
    }

    auto owned = map.into_iter();
    EXPECT_TRUE(map.is_empty());
    usize count = 0;
    for (auto item = owned.next(); item.is_some(); item = owned.next()) ++count;
    EXPECT_EQ(count, 256u);
}

TEST(HashMap, MoveOnlyValuesHaveBalancedLifetimes) {
    EXPECT_EQ(TrackedHashValue::live, 0);
    {
        auto map = HashMap<i32, TrackedHashValue>::make();
        for (i32 i = 0; i < 300; ++i) map.insert(i, TrackedHashValue(i));
        auto old = map.insert(8, TrackedHashValue(800));
        ASSERT_TRUE(old.is_some());
        EXPECT_EQ(old->value, 8);
        auto removed = map.remove(9);
        ASSERT_TRUE(removed.is_some());
        EXPECT_EQ(removed->value, 9);
        map.reserve(500);
        map.clear();
    }
    EXPECT_EQ(TrackedHashValue::live, 0);
}

TEST(HashMap, RandomizedOperationsMatchModel) {
    constexpr usize key_count = 311;
    bool            present[key_count] {};
    i32             values[key_count] {};
    usize           expected_len = 0;
    u32             state        = 0x87654321u;
    auto            map          = HashMap<i32, i32>::make();

    for (i32 operation = 0; operation < 12000; ++operation) {
        state   = state * 1664525u + 1013904223u;
        i32 key = static_cast<i32>((state >> 7) % key_count);
        if (state % 3 == 0) {
            i32  value = operation * 5;
            auto old   = map.insert(key, value);
            EXPECT_EQ(old.is_some(), present[key]);
            if (present[key]) EXPECT_EQ(*old, values[key]);
            if (! present[key]) ++expected_len;
            present[key] = true;
            values[key]  = value;
        } else if (state % 3 == 1) {
            auto removed = map.remove(key);
            EXPECT_EQ(removed.is_some(), present[key]);
            if (present[key]) {
                EXPECT_EQ(*removed, values[key]);
                present[key] = false;
                --expected_len;
            }
        } else {
            auto value = map.get(key);
            EXPECT_EQ(value.is_some(), present[key]);
            if (present[key]) EXPECT_EQ(**value, values[key]);
        }
        if (operation % 53 == 0) EXPECT_EQ(map.len(), expected_len);
    }
}

TEST(HashMap, StringKeysUseTheirHashOwner) {
    auto map = HashMap<rstd::string::String, i32>::make();
    map.insert(rstd::string::String::make("alpha"), 1);
    map.insert(rstd::string::String::make("beta"), 2);
    auto key = rstd::string::String::make("alpha");
    EXPECT_EQ(**map.get(key), 1);
}

TEST(HashMap, CustomHashKeepsTheStoredEquivalentKey) {
    auto map = HashMap<HashKey, i32>::make();
    for (i32 i = 0; i < 200; ++i) map.insert(HashKey(i, 1000 + i), i);

    EXPECT_EQ(map.insert(HashKey(5, 9999), 500), Some(5));
    HashKey lookup(5, 0);
    auto    entry = map.get_key_value(lookup);
    ASSERT_TRUE(entry.is_some());
    EXPECT_EQ(entry->get<0>()->identity, 1005);
    EXPECT_EQ(*entry->get<1>(), 500);
    EXPECT_EQ(map.remove(lookup), Some(500));
}

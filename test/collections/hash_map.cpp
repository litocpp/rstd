#include <gtest/gtest.h>
import rstd;

using namespace rstd::prelude;
using rstd::collections::HashMap;
namespace iter = rstd::iter;

namespace
{

struct ConstantHasher {
    void write(rstd::slice<rstd::byte>) noexcept {}
    auto finish() const noexcept -> u64 { return u64(7); }
};

using ConstantBuildHasher = rstd::hash::BuildHasherDefault<ConstantHasher>;

struct SeededHasher {
    u64 value;

    explicit SeededHasher(u64 seed) noexcept: value(seed) {}

    void write(rstd::slice<rstd::byte> bytes) noexcept {
        for (usize i {}; i < bytes.len(); ++i) value = value.wrapping_add(u64(bytes[i]));
    }

    auto finish() const noexcept -> u64 { return value; }
};

struct SeededBuilder {
    using Hasher = SeededHasher;

    u64 seed;

    auto build_hasher() const noexcept -> Hasher { return Hasher(seed); }
};

struct ExplicitBuilder {
    u64 seed;
};

struct CapturingHasher {
    rstd::byte   data[64] {};
    rstd::size_t length {};

    void write(rstd::slice<rstd::byte> bytes) noexcept {
        for (rstd::size_t i = 0; i < bytes.len().to_primitive(); ++i) {
            data[length++] = bytes[usize(i)];
        }
    }

    auto finish() const noexcept -> u64 { return u64(length); }
};

struct TrackedHashValue {
    static inline int live = 0;
    int               value;

    explicit TrackedHashValue(int v): value(v) { ++live; }
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
    template<typename H>
        requires Impled<H, hash::Hasher>
    void hash(H& state) const noexcept {
        hash::write_value(state, this->self().id);
    }
};

template<>
struct Impl<hash::BuildHasher, ExplicitBuilder> : ImplBase<ExplicitBuilder> {
    using Hasher = SeededHasher;

    auto build_hasher() const noexcept -> Hasher { return Hasher(this->self().seed); }
};

} // namespace rstd

TEST(HashMap, BasicLookupReplacementAndCapacity) {
    auto map = HashMap<i32, i32>::make();
    EXPECT_EQ(map.capacity(), usize());
    EXPECT_TRUE(map.insert(i32(1), i32(10)).is_none());
    EXPECT_TRUE(map.insert(i32(2), i32(20)).is_none());
    EXPECT_EQ(map.insert(i32(1), i32(100)), Some(i32(10)));
    EXPECT_EQ(map.len(), usize(2));
    EXPECT_TRUE(map.contains_key(i32(1)));
    EXPECT_FALSE(map.contains_key(i32(3)));
    EXPECT_EQ(**map.get(i32(1)), i32(100));

    auto pair = map.get_key_value(i32(2));
    ASSERT_TRUE(pair.is_some());
    EXPECT_EQ(*pair->template get<0>(), i32(2));
    EXPECT_EQ(*pair->template get<1>(), i32(20));

    auto value = map.get_mut(i32(2));
    ASSERT_TRUE(value.is_some());
    **value = i32(200);
    EXPECT_EQ(**map.get(i32(2)), i32(200));

    map.reserve(usize(100));
    EXPECT_GE(map.capacity(), usize(102));
    map.clear();
    EXPECT_TRUE(map.is_empty());
    EXPECT_GE(map.capacity(), usize(100));
    map.shrink_to_fit();
    EXPECT_EQ(map.capacity(), usize());
}

TEST(HashMap, CollisionChainsSurviveTombstonesAndRehash) {
    auto map = HashMap<i32, i32, ConstantBuildHasher>::with_capacity(usize(1));
    for (i32 i {}; i < i32(300); i += i32(1)) map.insert(i, i * i32(2));
    for (i32 i {}; i < i32(150); i += i32(1)) {
        EXPECT_EQ(map.remove(i), Some(i * i32(2)));
    }
    for (i32 i = i32(150); i < i32(300); i += i32(1)) {
        EXPECT_EQ(**map.get(i), i * i32(2));
    }

    for (i32 i = i32(300); i < i32(500); i += i32(1)) map.insert(i, i * i32(2));
    for (i32 i = i32(150); i < i32(500); i += i32(1)) {
        EXPECT_EQ(**map.get(i), i * i32(2));
    }
    EXPECT_EQ(map.len(), usize(350));

    map.retain([](const i32& key, i32& value) {
        value += i32(1);
        return key % i32(2) == i32();
    });
    for (i32 i = i32(150); i < i32(500); i += i32(1)) {
        auto value = map.get(i);
        EXPECT_EQ(value.is_some(), i % i32(2) == i32());
        if (value.is_some()) EXPECT_EQ(**value, i * i32(2) + i32(1));
    }
}

TEST(HashMap, IteratorsAndCollectPreserveAllEntries) {
    auto map = iter::range(i32(), i32(256))
                   .map([](i32 key) {
                       return rstd::tuple<i32, i32>(key, key + i32(1));
                   })
                   .collect<HashMap<i32, i32>>();

    bool seen[256] {};
    auto entries = map.iter_mut();
    for (auto item = entries.next(); item.is_some(); item = entries.next()) {
        i32 key = *item->template get<0>();
        EXPECT_FALSE(seen[key.to_primitive()]);
        seen[key.to_primitive()] = true;
        *item->template get<1>() += i32(10);
    }
    for (i32 i {}; i < i32(256); i += i32(1)) {
        EXPECT_TRUE(seen[i.to_primitive()]);
        EXPECT_EQ(**map.get(i), i + i32(11));
    }

    auto owned = map.into_iter();
    EXPECT_TRUE(map.is_empty());
    usize count {};
    for (auto item = owned.next(); item.is_some(); item = owned.next()) ++count;
    EXPECT_EQ(count, usize(256));
}

TEST(HashMap, MoveOnlyValuesHaveBalancedLifetimes) {
    EXPECT_EQ(TrackedHashValue::live, 0);
    {
        auto map = HashMap<i32, TrackedHashValue>::make();
        for (i32 i {}; i < i32(300); i += i32(1)) {
            map.insert(i, TrackedHashValue(i.to_primitive()));
        }
        auto old = map.insert(i32(8), TrackedHashValue(800));
        ASSERT_TRUE(old.is_some());
        EXPECT_EQ(old->value, 8);
        auto removed = map.remove(i32(9));
        ASSERT_TRUE(removed.is_some());
        EXPECT_EQ(removed->value, 9);
        map.reserve(usize(500));
        map.clear();
    }
    EXPECT_EQ(TrackedHashValue::live, 0);
}

TEST(HashMap, RandomizedOperationsMatchModel) {
    constexpr rstd::size_t key_count = 311;
    bool                   present[key_count] {};
    i32                    values[key_count] {};
    usize                  expected_len {};
    u32                    state = u32(0x87654321u);
    auto                   map   = HashMap<i32, i32>::make();

    for (i32 operation {}; operation < i32(12000); operation += i32(1)) {
        state    = state * u32(1664525u) + u32(1013904223u);
        i32  key = as_cast<i32>((state >> u64(7)) % u32(static_cast<rstd::uint32_t>(key_count)));
        auto model_index = static_cast<rstd::size_t>(key.to_primitive());
        if (state % u32(3) == u32()) {
            i32  value = operation * i32(5);
            auto old   = map.insert(key, value);
            EXPECT_EQ(old.is_some(), present[model_index]);
            if (present[model_index]) EXPECT_EQ(*old, values[model_index]);
            if (! present[model_index]) ++expected_len;
            present[model_index] = true;
            values[model_index]  = value;
        } else if (state % u32(3) == u32(1)) {
            auto removed = map.remove(key);
            EXPECT_EQ(removed.is_some(), present[model_index]);
            if (present[model_index]) {
                EXPECT_EQ(*removed, values[model_index]);
                present[model_index] = false;
                --expected_len;
            }
        } else {
            auto value = map.get(key);
            EXPECT_EQ(value.is_some(), present[model_index]);
            if (present[model_index]) EXPECT_EQ(**value, values[model_index]);
        }
        if (operation % i32(53) == i32()) EXPECT_EQ(map.len(), expected_len);
    }
}

TEST(HashMap, StringKeysUseTheirHashOwner) {
    auto map = HashMap<rstd::string::String, i32>::make();
    map.insert(rstd::string::String::make("alpha"), i32(1));
    map.insert(rstd::string::String::make("beta"), i32(2));
    auto key = rstd::string::String::make("alpha");
    EXPECT_EQ(**map.get(key), i32(1));
}

TEST(HashMap, CustomHashCombinesWithDifferentBuilders) {
    HashKey first(i32(5), i32(10));
    HashKey equivalent(i32(5), i32(20));

    auto random = rstd::hash::RandomState(u64(11), u64(19));
    EXPECT_EQ(rstd::hash::hash_one(random, first), rstd::hash::hash_one(random, equivalent));

    auto seeded = SeededBuilder { u64(23) };
    EXPECT_EQ(rstd::hash::hash_one(seeded, first), rstd::hash::hash_one(seeded, equivalent));

    auto map = HashMap<HashKey, i32, SeededBuilder>::with_hasher(SeededBuilder { u64(23) });
    map.insert(HashKey(i32(5), i32(30)), i32(7));
    EXPECT_EQ(**map.get(first), i32(7));
}

TEST(HashMap, BuilderCreatesIndependentEquivalentStates) {
    auto builder = SeededBuilder { u64(31) };
    auto first   = rstd::hash::hash_one(builder, i32(7));
    auto second  = rstd::hash::hash_one(builder, i32(7));
    EXPECT_EQ(first, second);

    auto other = SeededBuilder { u64(32) };
    EXPECT_NE(first, rstd::hash::hash_one(other, i32(7)));
}

TEST(HashMap, ExplicitBuildHasherImplUsesTheCommonHashPath) {
    auto builder = ExplicitBuilder { u64(37) };
    EXPECT_EQ(rstd::hash::hash_one(builder, i32(9)), rstd::hash::hash_one(builder, i32(9)));

    auto map = HashMap<HashKey, i32, ExplicitBuilder>::with_hasher(ExplicitBuilder { u64(37) });
    map.insert(HashKey(i32(9), i32(1)), i32(11));
    EXPECT_EQ(**map.get(HashKey(i32(9), i32(2))), i32(11));
}

TEST(Hash, FinishIsStableWithoutAdditionalWrites) {
    rstd::hash::DefaultHasher state(u64(3), u64(5));
    rstd::hash::write_value(state, i32(7));
    auto first = state.finish();
    EXPECT_EQ(first, state.finish());
}

TEST(HashMap, PointerKeysHashTheirAddress) {
    int  marker = 0;
    auto map    = HashMap<const void*, i32>::make();

    map.insert(nullptr, i32(1));
    map.insert(&marker, i32(2));

    EXPECT_EQ(**map.get(nullptr), i32(1));
    EXPECT_EQ(**map.get(&marker), i32(2));
}

TEST(Hash, StringHashPreservesFieldBoundaries) {
    auto ab = rstd::string::String::make("ab");
    auto c  = rstd::string::String::make("c");
    auto a  = rstd::string::String::make("a");
    auto bc = rstd::string::String::make("bc");

    CapturingHasher left;
    rstd::hash::hash_into(ab, left);
    rstd::hash::hash_into(c, left);

    CapturingHasher right;
    rstd::hash::hash_into(a, right);
    rstd::hash::hash_into(bc, right);

    ASSERT_EQ(left.length, right.length);
    bool differs = false;
    for (rstd::size_t i = 0; i < left.length; ++i) differs |= left.data[i] != right.data[i];
    EXPECT_TRUE(differs);
}

TEST(HashMap, CustomHashKeepsTheStoredEquivalentKey) {
    auto map = HashMap<HashKey, i32>::make();
    for (i32 i {}; i < i32(200); i += i32(1)) {
        map.insert(HashKey(i, i32(1000) + i), i);
    }

    EXPECT_EQ(map.insert(HashKey(i32(5), i32(9999)), i32(500)), Some(i32(5)));
    HashKey lookup(i32(5), i32());
    auto    entry = map.get_key_value(lookup);
    ASSERT_TRUE(entry.is_some());
    EXPECT_EQ(entry->template get<0>()->identity, i32(1005));
    EXPECT_EQ(*entry->template get<1>(), i32(500));
    EXPECT_EQ(map.remove(lookup), Some(i32(500)));
}

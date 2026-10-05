module;
#include <rstd/test/gtest.hpp>

export module rstd.alloc:btree_tests;
import :collections.btree_map;

using namespace rstd::prelude;

namespace alloc::collections
{
struct BTreeMapTestAccess {
    template<typename K, typename V>
    static bool validate_node(const Node<K, V>& node,
                              bool              is_root,
                              const K*          lower,
                              const K*          upper,
                              usize             depth,
                              usize&            leaf_depth,
                              bool&             saw_leaf,
                              usize&            count) {
        if (node.len > usize(CAPACITY) || (! is_root && node.len < usize(B - 1))) return false;
        if (! node.leaf && node.len == usize()) return false;
        for (usize i {}; i < node.len; ++i) {
            const auto& key = node.key(i);
            if (lower != nullptr && ! (*lower < key)) return false;
            if (upper != nullptr && ! (key < *upper)) return false;
            if (i != usize() && ! (node.key(i - usize(1)) < key)) return false;
        }
        count += node.len;
        if (node.leaf) {
            if (saw_leaf && leaf_depth != depth) return false;
            leaf_depth = depth;
            saw_leaf   = true;
            return true;
        }
        for (usize i {}; i <= node.len; ++i) {
            const K* child_lower = i == usize() ? lower : rstd::addressof(node.key(i - usize(1)));
            const K* child_upper = i == node.len ? upper : rstd::addressof(node.key(i));
            if (! validate_node(*node.child(i),
                                false,
                                child_lower,
                                child_upper,
                                depth + usize(1),
                                leaf_depth,
                                saw_leaf,
                                count)) {
                return false;
            }
        }
        return true;
    }

    template<typename K, typename V>
    static bool valid(const BTreeMap<K, V>& map) {
        if (map.root.is_none()) return map.length == usize();
        usize leaf_depth {};
        usize count {};
        bool  saw_leaf = false;
        return validate_node(*map.root_node(),
                             true,
                             static_cast<const K*>(nullptr),
                             static_cast<const K*>(nullptr),
                             usize(),
                             leaf_depth,
                             saw_leaf,
                             count) &&
               count == map.length;
    }
};
} // namespace alloc::collections

using alloc::collections::BTreeMap;
using alloc::collections::BTreeMapTestAccess;

TEST(BTreeMap, ExplicitStructuralValidation) {
    BTreeMap<i32, i32> map;
    ASSERT_TRUE(BTreeMapTestAccess::valid(map));
    for (int key = 0; key < 1500; ++key) {
        map.insert(i32(key), i32(key));
        ASSERT_TRUE(BTreeMapTestAccess::valid(map));
    }
    for (int key = 0; key < 1500; ++key) {
        map.insert(i32(key), i32(-key));
        ASSERT_TRUE(BTreeMapTestAccess::valid(map));
    }
    auto moved = rstd::move(map);
    ASSERT_TRUE(BTreeMapTestAccess::valid(map));
    ASSERT_TRUE(BTreeMapTestAccess::valid(moved));
    map = rstd::move(moved);
    ASSERT_TRUE(BTreeMapTestAccess::valid(moved));
    for (int key = 0; key < 1500; key += 3) {
        ASSERT_TRUE(map.remove(i32(key)).is_some());
        ASSERT_TRUE(BTreeMapTestAccess::valid(map));
        ASSERT_TRUE(map.remove(i32(key)).is_none());
        ASSERT_TRUE(BTreeMapTestAccess::valid(map));
    }
    bool front = true;
    while (! map.is_empty()) {
        ASSERT_TRUE((front ? map.pop_first() : map.pop_last()).is_some());
        ASSERT_TRUE(BTreeMapTestAccess::valid(map));
        front = ! front;
    }
    map.clear();
    ASSERT_TRUE(BTreeMapTestAccess::valid(map));
}

struct CountedBTreeKey {
    int                        value;
    static inline rstd::size_t comparisons = 0;

    friend bool operator<(const CountedBTreeKey& left, const CountedBTreeKey& right) {
        ++comparisons;
        return left.value < right.value;
    }
};

TEST(BTreeMap, MutationsDoNotScanTheTree) {
    BTreeMap<CountedBTreeKey, i32> map;
    constexpr int                  count = 8192;
    for (int key = 0; key < count; ++key) {
        CountedBTreeKey::comparisons = 0;
        map.insert(CountedBTreeKey { key }, i32(key));
        // A loose path-search bound; a whole-tree validation exceeds it.
        ASSERT_TRUE(CountedBTreeKey::comparisons < 256);
    }
    ASSERT_TRUE(BTreeMapTestAccess::valid(map));
    for (int key = 0; key < count; ++key) {
        CountedBTreeKey::comparisons = 0;
        ASSERT_TRUE(map.insert(CountedBTreeKey { key }, i32(-key)).is_some());
        ASSERT_TRUE(CountedBTreeKey::comparisons < 256);
    }
    for (int key = 0; key < count / 2; ++key) {
        CountedBTreeKey::comparisons = 0;
        ASSERT_TRUE(map.remove(CountedBTreeKey { key }).is_some());
        ASSERT_TRUE(CountedBTreeKey::comparisons < 256);
        CountedBTreeKey::comparisons = 0;
        ASSERT_TRUE(map.remove(CountedBTreeKey { key }).is_none());
        ASSERT_TRUE(CountedBTreeKey::comparisons < 256);
    }
    ASSERT_TRUE(BTreeMapTestAccess::valid(map));
    while (! map.is_empty()) {
        CountedBTreeKey::comparisons = 0;
        ASSERT_TRUE(map.pop_first().is_some());
        ASSERT_TRUE(map.pop_last().is_some());
        ASSERT_EQ(CountedBTreeKey::comparisons, rstd::size_t(0));
    }
    ASSERT_TRUE(BTreeMapTestAccess::valid(map));
}

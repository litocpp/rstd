module;
#include <rstd/macro.hpp>

export module rstd.alloc:collections.btree_node;
export import :boxed;
export import rstd.core;

namespace alloc::collections::btree::detail
{

using ::alloc::boxed::Box;
using rstd::mem::maybe_uninit::MaybeUninit;
using namespace rstd::prelude;

inline constexpr usize B          = 6;
inline constexpr usize CAPACITY   = 2 * B - 1;
inline constexpr usize EDGE_COUNT = 2 * B;

template<typename K, typename V>
class Node {
    MaybeUninit<K>         keys[CAPACITY];
    MaybeUninit<V>         values[CAPACITY];
    MaybeUninit<Box<Node>> edges[EDGE_COUNT];

public:
    usize len;
    bool  leaf;

    explicit Node(bool is_leaf): len(0), leaf(is_leaf) {}
    Node(const Node&)            = delete;
    Node& operator=(const Node&) = delete;
    Node(Node&&)                 = delete;
    Node& operator=(Node&&)      = delete;

    ~Node() {
        for (usize i = 0; i < len; ++i) destroy_entry(i);
        if (! leaf) {
            for (usize i = 0; i <= len; ++i) edges[i].assume_init_drop();
        }
    }

    auto key(usize index) noexcept -> K& { return keys[index].assume_init_mut(); }
    auto key(usize index) const noexcept -> const K& { return keys[index].assume_init_ref(); }
    auto value(usize index) noexcept -> V& { return values[index].assume_init_mut(); }
    auto value(usize index) const noexcept -> const V& { return values[index].assume_init_ref(); }
    auto edge(usize index) noexcept -> Box<Node>& { return edges[index].assume_init_mut(); }
    auto edge(usize index) const noexcept -> const Box<Node>& {
        return edges[index].assume_init_ref();
    }
    auto child(usize index) noexcept -> Node* { return edge(index).get(); }
    auto child(usize index) const noexcept -> const Node* {
        return edge(index).as_ptr().as_raw_ptr();
    }

    void write_entry(usize index, K key, V value) {
        keys[index].write(rstd::move(key));
        values[index].write(rstd::move(value));
    }

    void destroy_entry(usize index) noexcept {
        keys[index].assume_init_drop();
        values[index].assume_init_drop();
    }

    auto take_entry(usize index) -> rstd::tuple<K, V> {
        K key   = rstd::move(this->key(index));
        V value = rstd::move(this->value(index));
        destroy_entry(index);
        return { rstd::move(key), rstd::move(value) };
    }

    void move_entry(usize source, usize destination) {
        auto entry = take_entry(source);
        write_entry(
            destination, rstd::move(entry.template get<0>()), rstd::move(entry.template get<1>()));
    }

    void insert_entry(usize index, K key, V value) {
        for (usize i = len; i > index; --i) move_entry(i - 1, i);
        write_entry(index, rstd::move(key), rstd::move(value));
        ++len;
    }

    auto remove_entry(usize index) -> rstd::tuple<K, V> {
        auto removed = take_entry(index);
        for (usize i = index; i + 1 < len; ++i) move_entry(i + 1, i);
        --len;
        return removed;
    }

    void write_edge(usize index, Box<Node> edge) { edges[index].write(rstd::move(edge)); }

    auto take_edge(usize index) -> Box<Node> {
        Box<Node> out = rstd::move(edge(index));
        edges[index].assume_init_drop();
        return out;
    }

    void move_edge(usize source, usize destination) { write_edge(destination, take_edge(source)); }
};

} // namespace alloc::collections::btree::detail

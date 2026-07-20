module;
#include <rstd/macro.hpp>

export module rstd.alloc:collections.hash_map;
export import :vec;
export import :hash.random;
export import :collections.hash_table;
export import rstd.core;

using ::alloc::vec::Vec;
using namespace rstd::prelude;

namespace alloc::collections
{

export template<typename K>
struct DefaultHashEqual {
    auto operator()(const K& left, const K& right) const noexcept -> bool { return left == right; }
};

export template<typename K,
                typename V,
                typename S  = rstd::hash::RandomState,
                typename Eq = DefaultHashEqual<K>>
class HashMap;
export template<typename K, typename V>
class HashMapIter;
export template<typename K, typename V>
class HashMapIterMut;
export template<typename K, typename V>
class HashMapIntoIter;
export template<typename K, typename V>
class HashMapKeys;
export template<typename K, typename V>
class HashMapValues;
export template<typename K, typename V>
class HashMapValuesMut;

export template<typename K, typename V>
class HashMapIter : public rstd::DefaultInClass<HashMapIter<K, V>, rstd::iter::Iterator> {
    const RawTable<K, V>* table;
    rstd::size_t          index;
    usize                 remaining;

public:
    using Item = rstd::tuple<rstd::ref<K>, rstd::ref<V>>;
    HashMapIter(const RawTable<K, V>* source [[clang::lifetimebound]]
                ,
                usize len)
        : table(source), index(0), remaining(len) {}

    auto next() -> Option<Item> {
        while (remaining != usize {} && index < table->bucket_count().to_primitive()) {
            const auto& bucket = table->bucket(index++);
            if (bucket.state != BucketState::Full) continue;
            --remaining;
            return Some(Item(rstd::ref<K>::from_raw_parts(rstd::addressof(bucket.key())),
                             rstd::ref<V>::from_raw_parts(rstd::addressof(bucket.value()))));
        }
        return None();
    }
    auto size_hint() const -> rstd::iter::SizeHint { return { remaining, Some(usize(remaining)) }; }
    auto len() const noexcept -> usize { return remaining; }
};

export template<typename K, typename V>
class HashMapIterMut : public rstd::DefaultInClass<HashMapIterMut<K, V>, rstd::iter::Iterator> {
    RawTable<K, V>* table;
    rstd::size_t    index;
    usize           remaining;

public:
    using Item = rstd::tuple<rstd::ref<K>, rstd::mut_ref<V>>;
    HashMapIterMut(RawTable<K, V>* source [[clang::lifetimebound]]
                   ,
                   usize len)
        : table(source), index(0), remaining(len) {}

    auto next() -> Option<Item> {
        while (remaining != usize {} && index < table->bucket_count().to_primitive()) {
            auto& bucket = table->bucket(index++);
            if (bucket.state != BucketState::Full) continue;
            --remaining;
            return Some(Item(rstd::ref<K>::from_raw_parts(rstd::addressof(bucket.key())),
                             rstd::mut_ref<V>::from_raw_parts(rstd::addressof(bucket.value()))));
        }
        return None();
    }
    auto size_hint() const -> rstd::iter::SizeHint { return { remaining, Some(usize(remaining)) }; }
    auto len() const noexcept -> usize { return remaining; }
};

export template<typename K, typename V>
class HashMapKeys : public rstd::DefaultInClass<HashMapKeys<K, V>, rstd::iter::Iterator> {
    HashMapIter<K, V> inner;

public:
    using Item = rstd::ref<K>;
    explicit HashMapKeys(HashMapIter<K, V> iter [[clang::lifetimebound]])
        : inner(rstd::move(iter)) {}
    auto next() -> Option<Item> {
        auto item = inner.next();
        return item.is_some() ? Some(item->template get<0>()) : None();
    }
    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const noexcept -> usize { return inner.len(); }
};

export template<typename K, typename V>
class HashMapValues : public rstd::DefaultInClass<HashMapValues<K, V>, rstd::iter::Iterator> {
    HashMapIter<K, V> inner;

public:
    using Item = rstd::ref<V>;
    explicit HashMapValues(HashMapIter<K, V> iter [[clang::lifetimebound]])
        : inner(rstd::move(iter)) {}
    auto next() -> Option<Item> {
        auto item = inner.next();
        return item.is_some() ? Some(item->template get<1>()) : None();
    }
    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const noexcept -> usize { return inner.len(); }
};

export template<typename K, typename V>
class HashMapValuesMut : public rstd::DefaultInClass<HashMapValuesMut<K, V>, rstd::iter::Iterator> {
    HashMapIterMut<K, V> inner;

public:
    using Item = rstd::mut_ref<V>;
    explicit HashMapValuesMut(HashMapIterMut<K, V> iter [[clang::lifetimebound]])
        : inner(rstd::move(iter)) {}
    auto next() -> Option<Item> {
        auto item = inner.next();
        return item.is_some() ? Some(item->template get<1>()) : None();
    }
    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const noexcept -> usize { return inner.len(); }
};

export template<typename K, typename V>
class HashMapIntoIter : public rstd::DefaultInClass<HashMapIntoIter<K, V>, rstd::iter::Iterator> {
    RawTable<K, V> table;
    rstd::size_t   index;

public:
    using Item = rstd::tuple<K, V>;
    explicit HashMapIntoIter(RawTable<K, V> source): table(rstd::move(source)), index(0) {}
    auto next() -> Option<Item> {
        while (index < table.bucket_count().to_primitive()) {
            rstd::size_t current = index++;
            if (table.bucket(current).state == BucketState::Full) {
                return Some(table.remove(usize(current)));
            }
        }
        return None();
    }
    auto size_hint() const -> rstd::iter::SizeHint { return { table.len(), Some(table.len()) }; }
    auto len() const noexcept -> usize { return table.len(); }
};

export template<typename K, typename V, typename S, typename Eq>
class HashMap {
    using Entry = rstd::tuple<K, V>;

    RawTable<K, V> table;
    S              hash_builder;
    Eq             equal;

    auto hash_key(const K& key) const noexcept -> u64
        requires rstd::hash::HashableBy<K, S>
    {
        return rstd::hash::hash_one(hash_builder, key);
    }

    auto find_index(const K& key) const -> Option<usize>
        requires rstd::hash::HashableBy<K, S>
    {
        u64 hash = hash_key(key);
        return table.find(hash, [&](const K& stored) {
            return equal(stored, key);
        });
    }

public:
    USE_TRAIT(HashMap)
    using IntoIter = HashMapIntoIter<K, V>;

    HashMap()
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq>
        : table(), hash_builder(), equal() {}
    HashMap(const HashMap&)                = delete;
    HashMap& operator=(const HashMap&)     = delete;
    HashMap(HashMap&&) noexcept            = default;
    HashMap& operator=(HashMap&&) noexcept = default;

    static auto make() -> HashMap
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq>
    {
        return {};
    }
    static auto with_capacity(usize capacity) -> HashMap
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq>
    {
        return HashMap(capacity, S {}, Eq {});
    }
    static auto with_hasher(S hasher) -> HashMap
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<Eq>
    {
        return HashMap(usize {}, rstd::move(hasher), Eq {});
    }
    static auto with_capacity_and_hasher(usize capacity, S hasher) -> HashMap
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<Eq>
    {
        return HashMap(capacity, rstd::move(hasher), Eq {});
    }

    HashMap(usize capacity, S hasher, Eq equality)
        requires rstd::hash::HashBuilder<S>
        : table(capacity), hash_builder(rstd::move(hasher)), equal(rstd::move(equality)) {}

    auto len() const noexcept -> usize { return table.len(); }
    auto is_empty() const noexcept -> bool { return table.len() == usize {}; }
    auto capacity() const noexcept -> usize { return table.capacity(); }
    auto hasher() const noexcept [[clang::lifetimebound]] -> const S& { return hash_builder; }

    void reserve(usize additional) { table.reserve(additional); }
    void shrink_to_fit() { table.shrink_to(usize {}); }
    void shrink_to(usize minimum) { table.shrink_to(minimum); }
    void clear() noexcept { table.clear(); }

    auto insert(K key, V value) -> Option<V>
        requires rstd::hash::HashableBy<K, S>
    {
        u64  hash  = hash_key(key);
        auto found = table.find(hash, [&](const K& stored) {
            return equal(stored, key);
        });
        if (found.is_some()) {
            return Some(table.bucket(*found).replace_value(rstd::move(value)));
        }
        table.insert(hash, rstd::move(key), rstd::move(value));
        return None();
    }

    auto get(const K& key) const [[clang::lifetimebound]] -> Option<rstd::ref<V>>
        requires rstd::hash::HashableBy<K, S>
    {
        auto found = find_index(key);
        if (found.is_none()) return None();
        return Some(rstd::ref<V>::from_raw_parts(rstd::addressof(table.bucket(*found).value())));
    }

    auto get_mut(const K& key) [[clang::lifetimebound]] -> Option<rstd::mut_ref<V>>
        requires rstd::hash::HashableBy<K, S>
    {
        auto found = find_index(key);
        if (found.is_none()) return None();
        return Some(
            rstd::mut_ref<V>::from_raw_parts(rstd::addressof(table.bucket(*found).value())));
    }

    auto get_key_value(const K& key) const [[clang::lifetimebound]]
    -> Option<rstd::tuple<rstd::ref<K>, rstd::ref<V>>>
        requires rstd::hash::HashableBy<K, S>
    {
        auto found = find_index(key);
        if (found.is_none()) return None();
        const auto& bucket = table.bucket(*found);
        return Some(rstd::tuple<rstd::ref<K>, rstd::ref<V>>(
            rstd::ref<K>::from_raw_parts(rstd::addressof(bucket.key())),
            rstd::ref<V>::from_raw_parts(rstd::addressof(bucket.value()))));
    }

    auto contains_key(const K& key) const -> bool
        requires rstd::hash::HashableBy<K, S>
    {
        return find_index(key).is_some();
    }

    auto remove_entry(const K& key) -> Option<Entry>
        requires rstd::hash::HashableBy<K, S>
    {
        auto found = find_index(key);
        return found.is_some() ? Some(table.remove(*found)) : None();
    }

    auto remove(const K& key) -> Option<V>
        requires rstd::hash::HashableBy<K, S>
    {
        auto entry = remove_entry(key);
        return entry.is_some() ? Some(rstd::move(entry->template get<1>())) : None();
    }

    template<typename F>
    void retain(F predicate) {
        for (rstd::size_t i = 0; i < table.bucket_count().to_primitive(); ++i) {
            auto& bucket = table.bucket(i);
            if (bucket.state == BucketState::Full && ! predicate(bucket.key(), bucket.value())) {
                (void)table.remove(usize(i));
            }
        }
    }

    auto iter() const [[clang::lifetimebound]] -> HashMapIter<K, V> {
        return { rstd::addressof(table), table.len() };
    }
    auto iter_mut() [[clang::lifetimebound]] -> HashMapIterMut<K, V> {
        return { rstd::addressof(table), table.len() };
    }
    auto keys() const [[clang::lifetimebound]] -> HashMapKeys<K, V> {
        return HashMapKeys<K, V>(iter());
    }
    auto values() const [[clang::lifetimebound]] -> HashMapValues<K, V> {
        return HashMapValues<K, V>(iter());
    }
    auto values_mut() [[clang::lifetimebound]] -> HashMapValuesMut<K, V> {
        return HashMapValuesMut<K, V>(iter_mut());
    }
    auto into_iter() -> IntoIter { return IntoIter(rstd::move(table)); }
};

} // namespace alloc::collections

namespace rstd
{

template<typename K, typename V, typename S, typename Eq>
    requires hash::HashableBy<K, S> && mtp::init<S> && mtp::init<Eq>
struct Impl<iter::FromIterator<tuple<K, V>>, ::alloc::collections::HashMap<K, V, S, Eq>>
    : ImplBase<::alloc::collections::HashMap<K, V, S, Eq>> {
    template<typename It>
    static auto from_iter(It iter) -> ::alloc::collections::HashMap<K, V, S, Eq> {
        auto map = ::alloc::collections::HashMap<K, V, S, Eq>::make();
        for (auto item = iter.next(); item.is_some(); item = iter.next()) {
            map.insert(rstd::move(item->template get<0>()), rstd::move(item->template get<1>()));
        }
        return map;
    }
};

template<typename K, typename V, typename S, typename Eq>
struct Impl<iter::IntoIterator, ::alloc::collections::HashMap<K, V, S, Eq>>
    : ImplBase<::alloc::collections::HashMap<K, V, S, Eq>> {
    auto into_iter() -> ::alloc::collections::HashMapIntoIter<K, V> {
        return this->self().into_iter();
    }
};

} // namespace rstd

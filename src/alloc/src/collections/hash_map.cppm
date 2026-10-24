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

template<typename K, typename V>
struct MapEntry {
    u64       hash;
    K         key_;
    Option<V> value_;
    MapEntry(u64 hash, K key, V value)
        : hash(hash), key_(rstd::move(key)), value_(Some(rstd::move(value))) {}
    auto key() const noexcept -> const K& { return key_; }
    auto value() -> V& { return *value_; }
    auto value() const -> const V& { return *value_; }
    auto replace_value(V replacement) -> V {
        auto old = value_.take();
        value_.insert(rstd::move(replacement));
        return rstd::move(old).unwrap();
    }
    auto into_pair() && -> rstd::tuple<K, V> {
        return { rstd::move(key_), rstd::move(value_).unwrap() };
    }
};

export template<typename K>
struct DefaultHashEqual {
    template<typename Left, typename Right>
    auto operator()(const Left& left, const Right& right) const noexcept -> bool {
        return left == right;
    }
};

export template<typename K,
                typename V,
                typename S  = rstd::hash::RandomState,
                typename Eq = DefaultHashEqual<K>,
                typename A  = ::alloc::Global>
class HashMap;
export template<typename K, typename V, typename A = ::alloc::Global>
class HashMapIter;
export template<typename K, typename V, typename A = ::alloc::Global>
class HashMapIterMut;
export template<typename K, typename V, typename A = ::alloc::Global>
class HashMapIntoIter;
export template<typename K, typename V, typename A = ::alloc::Global>
class HashMapKeys;
export template<typename K, typename V, typename A = ::alloc::Global>
class HashMapValues;
export template<typename K, typename V, typename A = ::alloc::Global>
class HashMapValuesMut;

export template<typename K, typename V, typename A>
class HashMapIter : public rstd::DefaultInClass<HashMapIter<K, V, A>, rstd::iter::Iterator> {
    typename HashTable<MapEntry<K, V>, A>::Iter inner;

public:
    using Item                               = rstd::tuple<rstd::ref<K>, rstd::ref<V>>;
    static constexpr bool PROVEN_EXACT_SIZE  = true;
    static constexpr bool PROVEN_FUSED       = true;
    static constexpr bool PROVEN_TRUSTED_LEN = true;
    explicit HashMapIter(typename HashTable<MapEntry<K, V>, A>::Iter source
                         [[clang::lifetimebound]])
        : inner(rstd::move(source)) {}
    auto next() -> Option<Item> {
        auto entry = inner.next();
        if (entry.is_none()) return None();
        return Some(Item(rstd::ref<K>::from_raw_parts(rstd::addressof((**entry).key())),
                         rstd::ref<V>::from_raw_parts(rstd::addressof((**entry).value()))));
    }
    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const noexcept -> usize { return inner.len(); }
};

export template<typename K, typename V, typename A>
class HashMapIterMut : public rstd::DefaultInClass<HashMapIterMut<K, V, A>, rstd::iter::Iterator> {
    typename HashTable<MapEntry<K, V>, A>::IterMut inner;

public:
    using Item                               = rstd::tuple<rstd::ref<K>, rstd::mut_ref<V>>;
    static constexpr bool PROVEN_EXACT_SIZE  = true;
    static constexpr bool PROVEN_FUSED       = true;
    static constexpr bool PROVEN_TRUSTED_LEN = true;
    explicit HashMapIterMut(typename HashTable<MapEntry<K, V>, A>::IterMut source
                            [[clang::lifetimebound]])
        : inner(rstd::move(source)) {}
    auto next() -> Option<Item> {
        auto entry = inner.next();
        if (entry.is_none()) return None();
        return Some(Item(rstd::ref<K>::from_raw_parts(rstd::addressof((**entry).key())),
                         rstd::mut_ref<V>::from_raw_parts(rstd::addressof((**entry).value()))));
    }
    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const noexcept -> usize { return inner.len(); }
};

export template<typename K, typename V, typename A>
class HashMapKeys : public rstd::DefaultInClass<HashMapKeys<K, V, A>, rstd::iter::Iterator> {
    HashMapIter<K, V, A> inner;

public:
    using Item                               = rstd::ref<K>;
    static constexpr bool PROVEN_EXACT_SIZE  = true;
    static constexpr bool PROVEN_FUSED       = true;
    static constexpr bool PROVEN_TRUSTED_LEN = true;
    explicit HashMapKeys(HashMapIter<K, V, A> iter [[clang::lifetimebound]])
        : inner(rstd::move(iter)) {}
    auto next() -> Option<Item> {
        auto item = inner.next();
        return item.is_some() ? Some(item->template get<0>()) : None();
    }
    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const noexcept -> usize { return inner.len(); }
};

export template<typename K, typename V, typename A>
class HashMapValues : public rstd::DefaultInClass<HashMapValues<K, V, A>, rstd::iter::Iterator> {
    HashMapIter<K, V, A> inner;

public:
    using Item                               = rstd::ref<V>;
    static constexpr bool PROVEN_EXACT_SIZE  = true;
    static constexpr bool PROVEN_FUSED       = true;
    static constexpr bool PROVEN_TRUSTED_LEN = true;
    explicit HashMapValues(HashMapIter<K, V, A> iter [[clang::lifetimebound]])
        : inner(rstd::move(iter)) {}
    auto next() -> Option<Item> {
        auto item = inner.next();
        return item.is_some() ? Some(item->template get<1>()) : None();
    }
    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const noexcept -> usize { return inner.len(); }
};

export template<typename K, typename V, typename A>
class HashMapValuesMut
    : public rstd::DefaultInClass<HashMapValuesMut<K, V, A>, rstd::iter::Iterator> {
    HashMapIterMut<K, V, A> inner;

public:
    using Item                               = rstd::mut_ref<V>;
    static constexpr bool PROVEN_EXACT_SIZE  = true;
    static constexpr bool PROVEN_FUSED       = true;
    static constexpr bool PROVEN_TRUSTED_LEN = true;
    explicit HashMapValuesMut(HashMapIterMut<K, V, A> iter [[clang::lifetimebound]])
        : inner(rstd::move(iter)) {}
    auto next() -> Option<Item> {
        auto item = inner.next();
        return item.is_some() ? Some(item->template get<1>()) : None();
    }
    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const noexcept -> usize { return inner.len(); }
};

export template<typename K, typename V, typename A>
class HashMapIntoIter
    : public rstd::DefaultInClass<HashMapIntoIter<K, V, A>, rstd::iter::Iterator> {
    typename HashTable<MapEntry<K, V>, A>::IntoIter inner;

public:
    using Item                               = rstd::tuple<K, V>;
    static constexpr bool PROVEN_EXACT_SIZE  = true;
    static constexpr bool PROVEN_FUSED       = true;
    static constexpr bool PROVEN_TRUSTED_LEN = true;
    explicit HashMapIntoIter(typename HashTable<MapEntry<K, V>, A>::IntoIter source)
        : inner(rstd::move(source)) {}
    auto next() -> Option<Item> {
        auto entry = inner.next();
        if (entry.is_none()) return None();
        return Some(rstd::move(*entry).into_pair());
    }
    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const noexcept -> usize { return inner.len(); }
};

export template<typename K, typename V, typename S, typename Eq, typename A>
class HashMap {
    using Entry = rstd::tuple<K, V>;

    using Stored = MapEntry<K, V>;
    HashTable<Stored, A> table;
    static auto          stored_hash(const Stored& entry) noexcept -> u64 { return entry.hash; }
    S                    hash_builder;
    Eq                   equal;

    auto hash_key(const K& key) const noexcept -> u64
        requires rstd::hash::HashableBy<K, S>
    {
        return rstd::hash::hash_one(hash_builder, key);
    }

    auto find_index(const K& key) const -> Option<ref<Stored>>
        requires rstd::hash::HashableBy<K, S>
    {
        u64 hash = hash_key(key);
        return table.find(hash, [&](const Stored& stored) {
            return stored.hash == hash && equal(stored.key(), key);
        });
    }

    template<typename Q>
    auto find_borrowed_index(ref<Q> key) const -> Option<ref<Stored>>
        requires rstd::Impled<K, rstd::borrow::Borrow<Q>> && rstd::hash::HashableBy<ref<Q>, S> &&
                 requires(const Eq& equality, ref<Q> left, ref<Q> right) {
                     { equality(left, right) } -> rstd::mtp::same_as<bool>;
                 }
    {
        u64 hash = rstd::hash::hash_one(hash_builder, key);
        return table.find(hash, [&](const Stored& stored) {
            if (stored.hash != hash) return false;
            auto borrowed = rstd::as<rstd::borrow::Borrow<Q>>(stored.key()).borrow();
            return equal(borrowed, key);
        });
    }

public:
    USE_TRAIT(HashMap)
    using IntoIter = HashMapIntoIter<K, V, A>;

    HashMap()
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq> &&
                     rstd::mtp::init<A>
        : table(), hash_builder(), equal() {}
    HashMap(const HashMap&)                = delete;
    HashMap& operator=(const HashMap&)     = delete;
    HashMap(HashMap&&) noexcept            = default;
    HashMap& operator=(HashMap&&) noexcept = default;

    static auto make() -> HashMap
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq> &&
                 rstd::mtp::init<A>
    {
        return {};
    }
    static auto with_capacity(usize capacity) -> HashMap
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq> &&
                 rstd::mtp::init<A>
    {
        return HashMap(capacity, S {}, Eq {}, A {});
    }
    static auto with_hasher(S hasher) -> HashMap
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<Eq> && rstd::mtp::init<A>
    {
        return HashMap(usize {}, rstd::move(hasher), Eq {}, A {});
    }
    static auto with_capacity_and_hasher(usize capacity, S hasher) -> HashMap
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<Eq> && rstd::mtp::init<A>
    {
        return HashMap(capacity, rstd::move(hasher), Eq {}, A {});
    }

    static auto new_in(A allocator) -> HashMap
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq>
    {
        return HashMap(usize {}, S {}, Eq {}, rstd::move(allocator));
    }

    static auto with_capacity_in(usize capacity, A allocator) -> HashMap
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq>
    {
        return HashMap(capacity, S {}, Eq {}, rstd::move(allocator));
    }

    HashMap(usize capacity, S hasher, Eq equality)
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<A>
        : HashMap(capacity, rstd::move(hasher), rstd::move(equality), A {}) {}

    HashMap(usize capacity, S hasher, Eq equality, A allocator)
        requires rstd::hash::HashBuilder<S>
        : table(HashTable<Stored, A>::with_capacity_in(capacity, rstd::move(allocator))),
          hash_builder(rstd::move(hasher)),
          equal(rstd::move(equality)) {}

    auto len() const noexcept -> usize { return table.len(); }
    auto is_empty() const noexcept -> bool { return table.len() == usize {}; }
    auto capacity() const noexcept -> usize { return table.capacity(); }
    auto hasher() const noexcept [[clang::lifetimebound]] -> const S& { return hash_builder; }
    auto allocator() const noexcept [[clang::lifetimebound]] -> const A& {
        return table.allocator();
    }

    template<typename B>
    auto clone_in(B allocator) const -> HashMap<K, V, S, Eq, B>
        requires rstd::Impled<K, rstd::clone::Clone> && rstd::Impled<V, rstd::clone::Clone> &&
                 rstd::Impled<S, rstd::clone::Clone> && rstd::Impled<Eq, rstd::clone::Clone>
    {
        auto result = HashMap<K, V, S, Eq, B>(table.len(),
                                              rstd::as<rstd::clone::Clone>(hash_builder).clone(),
                                              rstd::as<rstd::clone::Clone>(equal).clone(),
                                              rstd::move(allocator));
        auto source = iter();
        for (auto item = source.next(); item.is_some(); item = source.next()) {
            result.insert(rstd::as<rstd::clone::Clone>(*item->template get<0>()).clone(),
                          rstd::as<rstd::clone::Clone>(*item->template get<1>()).clone());
        }
        return result;
    }

    auto clone() const -> HashMap
        requires rstd::alloc::CloneableAllocator<A> &&
                 requires(const HashMap& source, A allocator) {
                     source.clone_in(rstd::move(allocator));
                 }
    {
        return clone_in(rstd::alloc::clone_allocator(allocator()));
    }

    void clone_from(const HashMap& source)
        requires requires(const HashMap& value) { value.clone(); }
    {
        if (this == &source) return;
        *this = source.clone_in(rstd::alloc::clone_allocator(allocator()));
    }

    /// Reserves for `additional` new entries; capacity/allocation errors leave the table unchanged.
    /// Without intervening removal or shrink, these inserts need no further table allocation.
    /// Throwing element moves provide resource safety, not rollback of the original contents.
    auto try_reserve(usize additional) -> Result<empty, TryReserveError> {
        return table.try_reserve(additional, stored_hash);
    }
    void reserve(usize additional) { table.reserve(additional, stored_hash); }
    void shrink_to_fit() { table.shrink_to(usize {}, stored_hash); }
    void shrink_to(usize minimum) { table.shrink_to(minimum, stored_hash); }
    void clear() noexcept { table.clear(); }

    auto insert(K key, V value) -> Option<V>
        requires rstd::hash::HashableBy<K, S>
    {
        u64  hash  = hash_key(key);
        auto entry = table.entry(
            hash,
            [&](const Stored& stored) {
                return stored.hash == hash && equal(stored.key(), key);
            },
            stored_hash);
        if (entry.is_Occupied()) {
            auto& occupied = entry.as_Occupied().value;
            struct ReplaceGuard {
                typename HashTable<Stored, A>::OccupiedEntry* entry;
                ~ReplaceGuard() {
                    if (entry) rstd::move(*entry).erase();
                }
            } guard { &occupied };
            auto old    = occupied.get_mut().replace_value(rstd::move(value));
            guard.entry = nullptr;
            return Some(rstd::move(old));
        }
        rstd::move(entry.as_Vacant().value)
            .insert(Stored(hash, rstd::move(key), rstd::move(value)));
        return None();
    }

    auto get(const K& key) const [[clang::lifetimebound]] -> Option<rstd::ref<V>>
        requires rstd::hash::HashableBy<K, S>
    {
        auto found = find_index(key);
        if (found.is_none()) return None();
        return Some(rstd::ref<V>::from_raw_parts(rstd::addressof((**found).value())));
    }

    template<typename Q>
    auto get(ref<Q> key) const [[clang::lifetimebound]] -> Option<rstd::ref<V>>
        requires(! rstd::mtp::same_as<K, ref<Q>>) && rstd::Impled<K, rstd::borrow::Borrow<Q>> &&
                rstd::hash::HashableBy<ref<Q>, S> &&
                requires(const Eq& equality, ref<Q> left, ref<Q> right) {
                    { equality(left, right) } -> rstd::mtp::same_as<bool>;
                }
    {
        auto found = find_borrowed_index(key);
        if (found.is_none()) return None();
        return Some(rstd::ref<V>::from_raw_parts(rstd::addressof((**found).value())));
    }

    auto get_mut(const K& key) [[clang::lifetimebound]] -> Option<rstd::mut_ref<V>>
        requires rstd::hash::HashableBy<K, S>
    {
        auto found = find_index(key);
        if (found.is_none()) return None();
        return Some(rstd::mut_ref<V>::from_raw_parts(
            rstd::addressof(const_cast<Stored&>(**found).value())));
    }

    template<typename Q>
    auto get_mut(ref<Q> key) [[clang::lifetimebound]] -> Option<rstd::mut_ref<V>>
        requires(! rstd::mtp::same_as<K, ref<Q>>) && rstd::Impled<K, rstd::borrow::Borrow<Q>> &&
                rstd::hash::HashableBy<ref<Q>, S> &&
                requires(const Eq& equality, ref<Q> left, ref<Q> right) {
                    { equality(left, right) } -> rstd::mtp::same_as<bool>;
                }
    {
        auto found = find_borrowed_index(key);
        if (found.is_none()) return None();
        return Some(rstd::mut_ref<V>::from_raw_parts(
            rstd::addressof(const_cast<Stored&>(**found).value())));
    }

    auto get_key_value(const K& key) const [[clang::lifetimebound]]
    -> Option<rstd::tuple<rstd::ref<K>, rstd::ref<V>>>
        requires rstd::hash::HashableBy<K, S>
    {
        auto found = find_index(key);
        if (found.is_none()) return None();
        const auto& bucket = **found;
        return Some(rstd::tuple<rstd::ref<K>, rstd::ref<V>>(
            rstd::ref<K>::from_raw_parts(rstd::addressof(bucket.key())),
            rstd::ref<V>::from_raw_parts(rstd::addressof(bucket.value()))));
    }

    template<typename Q>
    auto get_key_value(ref<Q> key) const [[clang::lifetimebound]]
    -> Option<rstd::tuple<rstd::ref<K>, rstd::ref<V>>>
        requires(! rstd::mtp::same_as<K, ref<Q>>) && rstd::Impled<K, rstd::borrow::Borrow<Q>> &&
                rstd::hash::HashableBy<ref<Q>, S> &&
                requires(const Eq& equality, ref<Q> left, ref<Q> right) {
                    { equality(left, right) } -> rstd::mtp::same_as<bool>;
                }
    {
        auto found = find_borrowed_index(key);
        if (found.is_none()) return None();
        const auto& bucket = **found;
        return Some(rstd::tuple<rstd::ref<K>, rstd::ref<V>>(
            rstd::ref<K>::from_raw_parts(rstd::addressof(bucket.key())),
            rstd::ref<V>::from_raw_parts(rstd::addressof(bucket.value()))));
    }

    auto contains_key(const K& key) const -> bool
        requires rstd::hash::HashableBy<K, S>
    {
        return find_index(key).is_some();
    }

    template<typename Q>
    auto contains_key(ref<Q> key) const -> bool
        requires(! rstd::mtp::same_as<K, ref<Q>>) && rstd::Impled<K, rstd::borrow::Borrow<Q>> &&
                rstd::hash::HashableBy<ref<Q>, S> &&
                requires(const Eq& equality, ref<Q> left, ref<Q> right) {
                    { equality(left, right) } -> rstd::mtp::same_as<bool>;
                }
    {
        return find_borrowed_index(key).is_some();
    }

    auto remove_entry(const K& key) -> Option<Entry>
        requires rstd::hash::HashableBy<K, S>
    {
        auto found = find_index(key);
        if (found.is_none()) return None();
        auto entry = table.find_entry((**found).hash, [&](const Stored& stored) {
            return &stored == (*found).as_raw_ptr();
        });
        return Some(rstd::move(rstd::move(*entry).remove()).into_pair());
    }

    template<typename Q>
    auto remove_entry(ref<Q> key) -> Option<Entry>
        requires(! rstd::mtp::same_as<K, ref<Q>>) && rstd::Impled<K, rstd::borrow::Borrow<Q>> &&
                rstd::hash::HashableBy<ref<Q>, S> &&
                requires(const Eq& equality, ref<Q> left, ref<Q> right) {
                    { equality(left, right) } -> rstd::mtp::same_as<bool>;
                }
    {
        auto found = find_borrowed_index(key);
        if (found.is_none()) return None();
        auto entry = table.find_entry((**found).hash, [&](const Stored& stored) {
            return &stored == (*found).as_raw_ptr();
        });
        return Some(rstd::move(rstd::move(*entry).remove()).into_pair());
    }

    auto remove(const K& key) -> Option<V>
        requires rstd::hash::HashableBy<K, S>
    {
        auto entry = remove_entry(key);
        return entry.is_some() ? Some(rstd::move(entry->template get<1>())) : None();
    }

    template<typename Q>
    auto remove(ref<Q> key) -> Option<V>
        requires(! rstd::mtp::same_as<K, ref<Q>>) && rstd::Impled<K, rstd::borrow::Borrow<Q>> &&
                rstd::hash::HashableBy<ref<Q>, S> &&
                requires(const Eq& equality, ref<Q> left, ref<Q> right) {
                    { equality(left, right) } -> rstd::mtp::same_as<bool>;
                }
    {
        auto entry = remove_entry(key);
        return entry.is_some() ? Some(rstd::move(entry->template get<1>())) : None();
    }

    template<typename F>
    void retain(F predicate) {
        table.retain([&](Stored& stored) {
            return predicate(stored.key(), stored.value());
        });
    }

    auto iter() const [[clang::lifetimebound]] -> HashMapIter<K, V, A> {
        return HashMapIter<K, V, A>(table.iter());
    }
    auto iter_mut() [[clang::lifetimebound]] -> HashMapIterMut<K, V, A> {
        return HashMapIterMut<K, V, A>(table.iter_mut());
    }
    auto keys() const [[clang::lifetimebound]] -> HashMapKeys<K, V, A> {
        return HashMapKeys<K, V, A>(iter());
    }
    auto values() const [[clang::lifetimebound]] -> HashMapValues<K, V, A> {
        return HashMapValues<K, V, A>(iter());
    }
    auto values_mut() [[clang::lifetimebound]] -> HashMapValuesMut<K, V, A> {
        return HashMapValuesMut<K, V, A>(iter_mut());
    }
    auto into_iter() && -> IntoIter { return IntoIter(rstd::move(table).into_iter()); }
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
        Impl<iter::Extend<tuple<K, V>>, ::alloc::collections::HashMap<K, V, S, Eq>>::extend(
            map, rstd::move(iter));
        return map;
    }
};

template<typename K, typename V, typename S, typename Eq, typename A>
    requires hash::HashableBy<K, S> && mtp::init<S> && mtp::init<Eq>
struct Impl<iter::Extend<tuple<K, V>>, ::alloc::collections::HashMap<K, V, S, Eq, A>>
    : ImplBase<::alloc::collections::HashMap<K, V, S, Eq, A>> {
    template<iter::has_next It>
    static void extend(::alloc::collections::HashMap<K, V, S, Eq, A>& map, It iterator) {
        for (auto item = iterator.next(); item.is_some(); item = iterator.next())
            extend_one(map, rstd::move(*item));
    }

    static void extend_one(::alloc::collections::HashMap<K, V, S, Eq, A>& map, tuple<K, V>&& item) {
        map.insert(rstd::move(item.template get<0>()), rstd::move(item.template get<1>()));
    }
};

template<typename K, typename V, typename S, typename Eq, typename A>
struct Impl<iter::IntoIterator, ::alloc::collections::HashMap<K, V, S, Eq, A>>
    : ImplBase<::alloc::collections::HashMap<K, V, S, Eq, A>> {
    using IntoIter = ::alloc::collections::HashMapIntoIter<K, V, A>;

    auto into_iter() -> IntoIter { return rstd::move(this->self()).into_iter(); }
};

template<typename K, typename V, typename S, typename Eq, typename A>
struct Impl<iter::IntoIterator, ref<::alloc::collections::HashMap<K, V, S, Eq, A>>>
    : ImplBase<ref<::alloc::collections::HashMap<K, V, S, Eq, A>>> {
    using IntoIter = ::alloc::collections::HashMapIter<K, V, A>;

    auto into_iter() -> IntoIter { return this->self().as_raw_ptr()->iter(); }
};

template<typename K, typename V, typename S, typename Eq, typename A>
struct Impl<iter::IntoIterator, mut_ref<::alloc::collections::HashMap<K, V, S, Eq, A>>>
    : ImplBase<mut_ref<::alloc::collections::HashMap<K, V, S, Eq, A>>> {
    using IntoIter = ::alloc::collections::HashMapIterMut<K, V, A>;

    auto into_iter() -> IntoIter { return this->self().as_raw_ptr()->iter_mut(); }
};

} // namespace rstd

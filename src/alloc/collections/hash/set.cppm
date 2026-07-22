module;
#include <rstd/macro.hpp>

export module rstd.alloc:collections.hash_set;
export import :collections.hash_map;

using namespace rstd::prelude;

namespace alloc::collections
{

export template<typename K>
using HashSetIter = HashMapKeys<K, rstd::empty>;

export template<typename K>
class HashSetIntoIter : public rstd::DefaultInClass<HashSetIntoIter<K>, rstd::iter::Iterator> {
    HashMapIntoIter<K, rstd::empty> inner;

public:
    using Item = K;

    explicit HashSetIntoIter(HashMapIntoIter<K, rstd::empty> iter): inner(rstd::move(iter)) {}

    auto next() -> Option<Item> {
        auto item = inner.next();
        if (item.is_none()) return None();
        return Some(rstd::move(item->template get<0>()));
    }

    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const noexcept -> usize { return inner.len(); }
};

export template<typename K, typename S = rstd::hash::RandomState, typename Eq = DefaultHashEqual<K>>
class HashSet {
    HashMap<K, rstd::empty, S, Eq> map;

    explicit HashSet(HashMap<K, rstd::empty, S, Eq> source): map(rstd::move(source)) {}

public:
    USE_TRAIT(HashSet)
    using IntoIter = HashSetIntoIter<K>;

    HashSet()
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq>
    = default;
    HashSet(const HashSet&)                = delete;
    HashSet& operator=(const HashSet&)     = delete;
    HashSet(HashSet&&) noexcept            = default;
    HashSet& operator=(HashSet&&) noexcept = default;

    static auto make() -> HashSet
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq>
    {
        return HashSet(HashMap<K, rstd::empty, S, Eq>::make());
    }

    static auto with_capacity(usize capacity) -> HashSet
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq>
    {
        return HashSet(HashMap<K, rstd::empty, S, Eq>::with_capacity(capacity));
    }

    static auto with_hasher(S hasher) -> HashSet
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<Eq>
    {
        return HashSet(HashMap<K, rstd::empty, S, Eq>::with_hasher(rstd::move(hasher)));
    }

    auto len() const noexcept -> usize { return map.len(); }
    auto is_empty() const noexcept -> bool { return map.is_empty(); }
    auto capacity() const noexcept -> usize { return map.capacity(); }
    void reserve(usize additional) { map.reserve(additional); }
    void shrink_to_fit() { map.shrink_to_fit(); }
    void clear() noexcept { map.clear(); }

    auto clone() const -> HashSet
        requires requires(const HashMap<K, rstd::empty, S, Eq>& source) { source.clone(); }
    {
        return HashSet(map.clone());
    }

    void clone_from(HashSet& source)
        requires requires(const HashMap<K, rstd::empty, S, Eq>& value) { value.clone(); }
    {
        map = source.map.clone();
    }

    auto insert(K value) -> bool { return map.insert(rstd::move(value), rstd::empty {}).is_none(); }

    auto get(const K& value) const [[clang::lifetimebound]] -> Option<rstd::ref<K>> {
        auto entry = map.get_key_value(value);
        return entry.is_some() ? Some(entry->template get<0>()) : None();
    }

    template<typename Q>
    auto get(rstd::ref<Q> value) const [[clang::lifetimebound]] -> Option<rstd::ref<K>> {
        auto entry = map.get_key_value(value);
        return entry.is_some() ? Some(entry->template get<0>()) : None();
    }

    auto contains(const K& value) const -> bool { return map.contains_key(value); }

    template<typename Q>
    auto contains(rstd::ref<Q> value) const -> bool {
        return map.contains_key(value);
    }

    auto remove(const K& value) -> bool { return map.remove(value).is_some(); }

    template<typename Q>
    auto remove(rstd::ref<Q> value) -> bool {
        return map.remove(value).is_some();
    }

    auto take(const K& value) -> Option<K> {
        auto entry = map.remove_entry(value);
        return entry.is_some() ? Some(rstd::move(entry->template get<0>())) : None();
    }

    template<typename Q>
    auto take(rstd::ref<Q> value) -> Option<K> {
        auto entry = map.remove_entry(value);
        return entry.is_some() ? Some(rstd::move(entry->template get<0>())) : None();
    }

    template<typename F>
    void retain(F predicate) {
        map.retain([&](const K& key, rstd::empty&) {
            return predicate(key);
        });
    }

    auto iter() const [[clang::lifetimebound]] -> HashSetIter<K> { return map.keys(); }
    auto into_iter() -> IntoIter { return IntoIter(map.into_iter()); }
};

} // namespace alloc::collections

namespace rstd
{

template<typename K, typename S, typename Eq>
    requires hash::HashableBy<K, S> && mtp::init<S> && mtp::init<Eq>
struct Impl<iter::FromIterator<K>, ::alloc::collections::HashSet<K, S, Eq>>
    : ImplBase<::alloc::collections::HashSet<K, S, Eq>> {
    template<typename It>
    static auto from_iter(It iter) -> ::alloc::collections::HashSet<K, S, Eq> {
        auto set = ::alloc::collections::HashSet<K, S, Eq>::make();
        for (auto item = iter.next(); item.is_some(); item = iter.next()) {
            set.insert(rstd::move(*item));
        }
        return set;
    }
};

template<typename K, typename S, typename Eq>
struct Impl<iter::IntoIterator, ::alloc::collections::HashSet<K, S, Eq>>
    : ImplBase<::alloc::collections::HashSet<K, S, Eq>> {
    auto into_iter() -> ::alloc::collections::HashSetIntoIter<K> {
        return this->self().into_iter();
    }
};

} // namespace rstd

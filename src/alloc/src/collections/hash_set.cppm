module;
#include <rstd/macro.hpp>

export module rstd.alloc:collections.hash_set;
export import :collections.hash_map;

using namespace rstd::prelude;

namespace alloc::collections
{

export template<typename K>
using HashSetIter = HashMapKeys<K, rstd::empty>;

export template<typename K, typename A = ::alloc::Global>
class HashSetIntoIter : public rstd::DefaultInClass<HashSetIntoIter<K, A>, rstd::iter::Iterator> {
    HashMapIntoIter<K, rstd::empty, A> inner;

public:
    using Item                               = K;
    static constexpr bool PROVEN_EXACT_SIZE  = true;
    static constexpr bool PROVEN_FUSED       = true;
    static constexpr bool PROVEN_TRUSTED_LEN = true;

    explicit HashSetIntoIter(HashMapIntoIter<K, rstd::empty, A> iter): inner(rstd::move(iter)) {}

    auto next() -> Option<Item> {
        auto item = inner.next();
        if (item.is_none()) return None();
        return Some(rstd::move(item->template get<0>()));
    }

    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const noexcept -> usize { return inner.len(); }
};

export template<typename K,
                typename S  = rstd::hash::RandomState,
                typename Eq = DefaultHashEqual<K>,
                typename A  = ::alloc::Global>
class HashSet {
    template<typename, typename, typename, typename>
    friend class HashSet;
    HashMap<K, rstd::empty, S, Eq, A> map;

    explicit HashSet(HashMap<K, rstd::empty, S, Eq, A> source): map(rstd::move(source)) {}

public:
    USE_TRAIT(HashSet)
    using IntoIter = HashSetIntoIter<K, A>;

    HashSet()
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq> &&
                     rstd::mtp::init<A>
    = default;
    HashSet(const HashSet&)                = delete;
    HashSet& operator=(const HashSet&)     = delete;
    HashSet(HashSet&&) noexcept            = default;
    HashSet& operator=(HashSet&&) noexcept = default;

    static auto make() -> HashSet
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq> &&
                 rstd::mtp::init<A>
    {
        return HashSet(HashMap<K, rstd::empty, S, Eq, A>::make());
    }

    static auto with_capacity(usize capacity) -> HashSet
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<S> && rstd::mtp::init<Eq> &&
                 rstd::mtp::init<A>
    {
        return HashSet(HashMap<K, rstd::empty, S, Eq, A>::with_capacity(capacity));
    }

    static auto with_hasher(S hasher) -> HashSet
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<Eq> && rstd::mtp::init<A>
    {
        return HashSet(HashMap<K, rstd::empty, S, Eq, A>::with_hasher(rstd::move(hasher)));
    }

    static auto with_hasher_and_equal(S hasher, Eq equal) -> HashSet
        requires rstd::hash::HashBuilder<S> && rstd::mtp::init<A>
    {
        return HashSet(
            HashMap<K, rstd::empty, S, Eq, A>(usize(), rstd::move(hasher), rstd::move(equal)));
    }

    static auto new_in(A allocator) -> HashSet {
        return HashSet(HashMap<K, rstd::empty, S, Eq, A>::new_in(rstd::move(allocator)));
    }
    static auto with_capacity_in(usize capacity, A allocator) -> HashSet {
        return HashSet(
            HashMap<K, rstd::empty, S, Eq, A>::with_capacity_in(capacity, rstd::move(allocator)));
    }
    auto allocator() const noexcept [[clang::lifetimebound]] -> const A& { return map.allocator(); }

    template<typename B>
    auto clone_in(B allocator) const -> HashSet<K, S, Eq, B>
        requires requires(const HashMap<K, rstd::empty, S, Eq, A>& source, B target) {
            source.clone_in(rstd::move(target));
        }
    {
        return HashSet<K, S, Eq, B>(map.clone_in(rstd::move(allocator)));
    }

    auto len() const noexcept -> usize { return map.len(); }
    auto is_empty() const noexcept -> bool { return map.is_empty(); }
    auto capacity() const noexcept -> usize { return map.capacity(); }
    /// Reserves for `additional` new keys with the same failure guarantees as HashMap.
    auto try_reserve(usize additional) -> Result<empty, TryReserveError> {
        return map.try_reserve(additional);
    }
    void reserve(usize additional) { map.reserve(additional); }
    void shrink_to_fit() { map.shrink_to_fit(); }
    void clear() noexcept { map.clear(); }

    auto clone() const -> HashSet
        requires requires(const HashMap<K, rstd::empty, S, Eq, A>& source) { source.clone(); }
    {
        return HashSet(map.clone());
    }

    void clone_from(const HashSet& source)
        requires requires(const HashMap<K, rstd::empty, S, Eq, A>& value) { value.clone(); }
    {
        map.clone_from(source.map);
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
    auto into_iter() && -> IntoIter { return IntoIter(rstd::move(map).into_iter()); }
};

} // namespace alloc::collections

namespace rstd
{

template<typename K, typename S, typename Eq, typename A>
    requires hash::HashableBy<K, S> && mtp::init<S> && mtp::init<Eq> && mtp::init<A>
struct Impl<iter::FromIterator<K>, ::alloc::collections::HashSet<K, S, Eq, A>>
    : ImplBase<::alloc::collections::HashSet<K, S, Eq, A>> {
    template<typename It>
    static auto from_iter(It iter) -> ::alloc::collections::HashSet<K, S, Eq, A> {
        auto set = ::alloc::collections::HashSet<K, S, Eq, A>::make();
        Impl<iter::Extend<K>, ::alloc::collections::HashSet<K, S, Eq, A>>::extend(set,
                                                                                  rstd::move(iter));
        return set;
    }
};

template<typename K, typename S, typename Eq, typename A>
    requires hash::HashableBy<K, S> && mtp::init<S> && mtp::init<Eq>
struct Impl<iter::Extend<K>, ::alloc::collections::HashSet<K, S, Eq, A>>
    : ImplBase<::alloc::collections::HashSet<K, S, Eq, A>> {
    template<iter::has_next It>
    static void extend(::alloc::collections::HashSet<K, S, Eq, A>& set, It iterator) {
        for (auto item = iterator.next(); item.is_some(); item = iterator.next())
            extend_one(set, rstd::move(*item));
    }

    static void extend_one(::alloc::collections::HashSet<K, S, Eq, A>& set, K&& item) {
        set.insert(rstd::move(item));
    }
};

template<typename K, typename S, typename Eq, typename A>
struct Impl<iter::IntoIterator, ::alloc::collections::HashSet<K, S, Eq, A>>
    : ImplBase<::alloc::collections::HashSet<K, S, Eq, A>> {
    using IntoIter = ::alloc::collections::HashSetIntoIter<K, A>;

    auto into_iter() -> IntoIter { return rstd::move(this->self()).into_iter(); }
};

template<typename K, typename S, typename Eq, typename A>
struct Impl<iter::IntoIterator, ref<::alloc::collections::HashSet<K, S, Eq, A>>>
    : ImplBase<ref<::alloc::collections::HashSet<K, S, Eq, A>>> {
    using IntoIter = ::alloc::collections::HashSetIter<K>;

    auto into_iter() -> IntoIter { return this->self().as_raw_ptr()->iter(); }
};

template<typename K, typename S, typename Eq, typename A>
struct Impl<iter::IntoIterator, mut_ref<::alloc::collections::HashSet<K, S, Eq, A>>>
    : ImplBase<mut_ref<::alloc::collections::HashSet<K, S, Eq, A>>> {
    using IntoIter = ::alloc::collections::HashSetIter<K>;

    auto into_iter() -> IntoIter { return this->self().as_raw_ptr()->iter(); }
};

} // namespace rstd

module;
#include <rstd/macro.hpp>

export module rstd.alloc:collections.btree_set;
export import :collections.btree_map;

using namespace rstd::prelude;

namespace alloc::collections
{

export template<typename K>
using BTreeSetIter = BTreeMapKeys<K, rstd::empty>;

export template<typename K>
class BTreeSetIntoIter : public rstd::DefaultInClass<BTreeSetIntoIter<K>, rstd::iter::Iterator> {
    BTreeMapIntoIter<K, rstd::empty> inner;

public:
    using Item                                = K;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;

    explicit BTreeSetIntoIter(BTreeMapIntoIter<K, rstd::empty> iter): inner(rstd::move(iter)) {}

    auto next() -> Option<Item> {
        auto item = inner.next();
        if (item.is_none()) return None();
        return Some(rstd::move(item->template get<0>()));
    }

    auto next_back() -> Option<Item> {
        auto item = inner.next_back();
        if (item.is_none()) return None();
        return Some(rstd::move(item->template get<0>()));
    }

    auto size_hint() const -> rstd::iter::SizeHint { return inner.size_hint(); }
    auto len() const -> usize { return inner.len(); }
};

export template<typename K>
class BTreeSet {
    BTreeMap<K, rstd::empty> map;

    explicit BTreeSet(BTreeMap<K, rstd::empty> source): map(rstd::move(source)) {}

public:
    USE_TRAIT(BTreeSet)
    using IntoIter = BTreeSetIntoIter<K>;

    BTreeSet()                               = default;
    BTreeSet(const BTreeSet&)                = delete;
    BTreeSet& operator=(const BTreeSet&)     = delete;
    BTreeSet(BTreeSet&&) noexcept            = default;
    BTreeSet& operator=(BTreeSet&&) noexcept = default;

    static auto make() -> BTreeSet { return BTreeSet(BTreeMap<K, rstd::empty>::make()); }

    auto len() const noexcept -> usize { return map.len(); }
    auto is_empty() const noexcept -> bool { return map.is_empty(); }
    void clear() { map.clear(); }

    auto clone() const -> BTreeSet
        requires requires(const BTreeMap<K, rstd::empty>& source) { source.clone(); }
    {
        return BTreeSet(map.clone());
    }

    void clone_from(BTreeSet& source)
        requires requires(const BTreeMap<K, rstd::empty>& value) { value.clone(); }
    {
        map = source.map.clone();
    }

    auto insert(K value) -> bool { return map.insert(rstd::move(value), rstd::empty {}).is_none(); }

    template<typename Q>
    auto get(const Q& value) const [[clang::lifetimebound]] -> Option<rstd::ref<K>> {
        auto entry = map.get_key_value(value);
        return entry.is_some() ? Some(entry->template get<0>()) : None();
    }

    template<typename Q>
    auto contains(const Q& value) const -> bool {
        return map.contains_key(value);
    }

    template<typename Q>
    auto remove(const Q& value) -> bool {
        return map.remove(value).is_some();
    }

    template<typename Q>
    auto take(const Q& value) -> Option<K> {
        auto entry = map.remove_entry(value);
        return entry.is_some() ? Some(rstd::move(entry->template get<0>())) : None();
    }

    template<typename F>
    void retain(F predicate) {
        auto source = map.into_iter();
        map         = BTreeMap<K, rstd::empty>::make();
        for (auto item = source.next(); item.is_some(); item = source.next()) {
            auto key = rstd::move(item->template get<0>());
            if (predicate(static_cast<const K&>(key))) {
                map.insert(rstd::move(key), rstd::empty {});
            }
        }
    }

    auto first() const [[clang::lifetimebound]] -> Option<rstd::ref<K>> {
        auto entry = map.first_key_value();
        return entry.is_some() ? Some(entry->template get<0>()) : None();
    }

    auto last() const [[clang::lifetimebound]] -> Option<rstd::ref<K>> {
        auto entry = map.last_key_value();
        return entry.is_some() ? Some(entry->template get<0>()) : None();
    }

    auto pop_first() -> Option<K> {
        auto entry = map.pop_first();
        return entry.is_some() ? Some(rstd::move(entry->template get<0>())) : None();
    }

    auto pop_last() -> Option<K> {
        auto entry = map.pop_last();
        return entry.is_some() ? Some(rstd::move(entry->template get<0>())) : None();
    }

    auto iter() const [[clang::lifetimebound]] -> BTreeSetIter<K> { return map.keys(); }
    auto into_iter() -> IntoIter { return IntoIter(map.into_iter()); }
};

} // namespace alloc::collections

namespace rstd
{

template<typename K>
struct Impl<iter::FromIterator<K>, ::alloc::collections::BTreeSet<K>>
    : ImplBase<::alloc::collections::BTreeSet<K>> {
    template<typename It>
    static auto from_iter(It iter) -> ::alloc::collections::BTreeSet<K> {
        auto set = ::alloc::collections::BTreeSet<K>::make();
        for (auto item = iter.next(); item.is_some(); item = iter.next()) {
            set.insert(rstd::move(*item));
        }
        return set;
    }
};

template<typename K>
struct Impl<iter::IntoIterator, ::alloc::collections::BTreeSet<K>>
    : ImplBase<::alloc::collections::BTreeSet<K>> {
    using IntoIter = ::alloc::collections::BTreeSetIntoIter<K>;

    auto into_iter() -> IntoIter { return this->self().into_iter(); }
};

template<typename K>
struct Impl<iter::IntoIterator, ref<::alloc::collections::BTreeSet<K>>>
    : ImplBase<ref<::alloc::collections::BTreeSet<K>>> {
    using IntoIter = ::alloc::collections::BTreeSetIter<K>;

    auto into_iter() -> IntoIter { return this->self().as_raw_ptr()->iter(); }
};

template<typename K>
struct Impl<iter::IntoIterator, mut_ref<::alloc::collections::BTreeSet<K>>>
    : ImplBase<mut_ref<::alloc::collections::BTreeSet<K>>> {
    using IntoIter = ::alloc::collections::BTreeSetIter<K>;

    auto into_iter() -> IntoIter { return this->self().as_raw_ptr()->iter(); }
};

} // namespace rstd

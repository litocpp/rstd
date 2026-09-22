export module rstd.alloc:iter.group;
export import :iter.storage;
export import :collections.hash_map;

using namespace rstd::prelude;
using ::alloc::vec::Vec;

template<typename K, typename V, typename S, typename Eq>
class KeyedValues {
    ::alloc::collections::HashMap<K, usize, S, Eq> indices_;
    Vec<V>                                         values_;

public:
    KeyedValues(S hasher, Eq equal): indices_(usize(), rstd::move(hasher), rstd::move(equal)) {}

    template<typename Init, typename F>
    decltype(auto) update(K key, Init initialize, F function) {
        auto index = indices_.get(key);
        if (index.is_some()) return function(values_[**index]);
        auto     position = values_.len();
        const K& observed = key;
        values_.push(initialize(observed));
        indices_.insert(rstd::move(key), position);
        return function(values_[position]);
    }

    auto get(const K& key) const [[clang::lifetimebound]] -> Option<rstd::ref<V>> {
        auto index = indices_.get(key);
        if (index.is_none()) return None();
        return Some(rstd::ref<V>::from_raw_parts(rstd::addressof(values_[**index])));
    }

    auto into_values() && -> Vec<rstd::tuple<K, V>> {
        using Pair   = rstd::tuple<K, V>;
        auto ordered = Vec<Option<Pair>>::with_capacity(values_.len());
        for (usize i; i < values_.len(); ++i) ordered.push(None<Pair>());
        auto indices = rstd::move(indices_).into_iter();
        for (auto entry = indices.next(); entry.is_some(); entry = indices.next()) {
            auto index = entry->template get<1>();
            ordered[index].insert(
                Pair(rstd::move(entry->template get<0>()), rstd::move(values_[index])));
        }
        return rstd::move(ordered)
            .into_iter()
            .map([](Option<Pair> pair) {
                return pair.unwrap();
            })
            .template collect<Vec<Pair>>();
    }
};

export namespace rstd::iter
{

template<typename K, typename T>
struct Group {
    K      key;
    Vec<T> items;
};

template<typename K,
         typename T,
         typename S  = hash::RandomState,
         typename Eq = ::alloc::collections::DefaultHashEqual<K>>
class GroupIndex {
    KeyedValues<K, Vec<T>, S, Eq> values_;

public:
    GroupIndex(S hasher, Eq equal): values_(rstd::move(hasher), rstd::move(equal)) {}
    void insert(K key, T item) {
        values_.update(
            rstd::move(key),
            [](const K&) {
                return Vec<T>();
            },
            [&](Vec<T>& group) {
                group.push(rstd::move(item));
            });
    }
    auto get(const K& key) const [[clang::lifetimebound]] -> Option<ref<Vec<T>>> {
        return values_.get(key);
    }
    auto into_groups() && -> Vec<Group<K, T>> {
        return rstd::move(values_)
            .into_values()
            .into_iter()
            .map([](tuple<K, Vec<T>> pair) {
                return Group<K, T> { rstd::move(pair.template get<0>()),
                                     rstd::move(pair.template get<1>()) };
            })
            .template collect<Vec<Group<K, T>>>();
    }
};

template<typename Input,
         typename F,
         typename S   = hash::RandomState,
         typename Key = decltype(mtp::declval<F&>()(
             mtp::declval<const mtp::rm_ref<typename into_iter_t<Input>::Item>&>())),
         typename Eq  = ::alloc::collections::DefaultHashEqual<Key>>
auto build_group_index(Input&& input, F key, S hasher = {}, Eq equal = {}) {
    using I = into_iter_t<Input>;
    check_persistent_key<typename I::Item, Key>();
    auto index  = GroupIndex<Key, stored_item_t<typename I::Item>, S, Eq>(rstd::move(hasher),
                                                                          rstd::move(equal));
    auto source = iter::into_iter(rstd::forward<Input>(input));
    for (auto item = source.next(); item.is_some(); item = source.next()) {
        const auto& observed = *item;
        auto        k        = key(observed);
        index.insert(rstd::move(k),
                     store_item<typename I::Item>(rstd::forward<typename I::Item>(*item)));
    }
    return index;
}

template<typename Input,
         typename F,
         typename S   = hash::RandomState,
         typename Key = decltype(mtp::declval<F&>()(
             mtp::declval<const mtp::rm_ref<typename into_iter_t<Input>::Item>&>())),
         typename Eq  = ::alloc::collections::DefaultHashEqual<Key>>
auto group_by(Input&& input, F key, S hasher = {}, Eq equal = {}) {
    return build_group_index(
               rstd::forward<Input>(input), rstd::move(key), rstd::move(hasher), rstd::move(equal))
        .into_groups();
}

template<typename Input,
         typename F,
         typename Init,
         typename Fold,
         typename S   = hash::RandomState,
         typename Key = decltype(mtp::declval<F&>()(
             mtp::declval<const mtp::rm_ref<typename into_iter_t<Input>::Item>&>())),
         typename Eq  = ::alloc::collections::DefaultHashEqual<Key>>
auto fold_by(Input&& input, F key, Init initialize, Fold fold, S hasher = {}, Eq equal = {}) {
    using I           = into_iter_t<Input>;
    using Accumulator = decltype(initialize(mtp::declval<const Key&>()));
    check_persistent_key<typename I::Item, Key>();
    static_assert(! mtp::is_ref<Accumulator>);
    KeyedValues<Key, Option<Accumulator>, S, Eq> values(rstd::move(hasher), rstd::move(equal));
    auto source = iter::into_iter(rstd::forward<Input>(input));
    for (auto item = source.next(); item.is_some(); item = source.next()) {
        const auto& observed = *item;
        values.update(
            key(observed),
            [&](const Key& k) {
                return Some(initialize(k));
            },
            [&](Option<Accumulator>& accumulator) {
                accumulator.insert(
                    fold(accumulator.take().unwrap(), rstd::forward<typename I::Item>(*item)));
            });
    }
    return rstd::move(values)
        .into_values()
        .into_iter()
        .map([](tuple<Key, Option<Accumulator>> pair) {
            return tuple<Key, Accumulator>(rstd::move(pair.template get<0>()),
                                           pair.template get<1>().unwrap());
        })
        .template collect<Vec<tuple<Key, Accumulator>>>();
}

enum class CountOverflow
{
    Overflow
};

constexpr auto increment_count(usize& count) -> Result<rstd::empty, CountOverflow> {
    if (count == usize::MAX) return Err(CountOverflow::Overflow);
    ++count;
    return Ok(rstd::empty {});
}

template<typename Input,
         typename F,
         typename S   = hash::RandomState,
         typename Key = decltype(mtp::declval<F&>()(
             mtp::declval<const mtp::rm_ref<typename into_iter_t<Input>::Item>&>())),
         typename Eq  = ::alloc::collections::DefaultHashEqual<Key>>
auto count_by(Input&& input, F key, S hasher = {}, Eq equal = {})
    -> Result<Vec<tuple<Key, usize>>, CountOverflow> {
    using I = into_iter_t<Input>;
    check_persistent_key<typename I::Item, Key>();
    KeyedValues<Key, usize, S, Eq> values(rstd::move(hasher), rstd::move(equal));
    auto                           source = iter::into_iter(rstd::forward<Input>(input));
    for (auto item = source.next(); item.is_some(); item = source.next()) {
        const auto& observed = *item;
        auto        result   = values.update(
            key(observed),
            [](const Key&) {
                return usize();
            },
            increment_count);
        if (result.is_err()) return Err(CountOverflow::Overflow);
    }
    return Ok(rstd::move(values).into_values());
}

} // namespace rstd::iter

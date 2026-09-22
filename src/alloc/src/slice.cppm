export module rstd.alloc:slice;
export import :vec;
import :iter.storage;
export import rstd.core;

using namespace rstd::prelude;
using ::alloc::vec::Vec;

template<typename T>
void apply_slice_order(rstd::mut_ref<T[]> values, Vec<usize>& order) {
    for (usize i; i < values.len(); ++i) {
        auto index = order[i];
        while (index < i) index = order[index];
        order[i] = index;
        rstd::slice_::swap(values, i, index);
    }
}

export namespace rstd::slice_
{

template<typename T, typename F>
void sort_by(mut_ref<T[]> values, F compare) {
    if (values.len() < usize(2)) return;
    auto order = iter::range(usize(), values.len()).template collect<Vec<usize>>();
    sort_unstable_by(order.as_mut_slice().as_mut_ref(), [&](usize a, usize b) {
        const T& left   = values[a];
        const T& right  = values[b];
        auto     result = compare(left, right);
        static_assert(mtp::same_as<decltype(result), strong_ordering>);
        return result == strong_ordering::less || (result == strong_ordering::equal && a < b);
    });
    apply_slice_order(values, order);
}

template<typename T, typename F>
void sort_by_key(mut_ref<T[]> values, F key) {
    sort_by(values, [&key](const T& a, const T& b) {
        return key(a) <=> key(b);
    });
}

template<typename T, typename F>
void sort_by_cached_key(mut_ref<T[]> values, F key) {
    using Key = decltype(key(mtp::declval<const T&>()));
    iter::check_persistent_key<T, Key>();
    if (values.len() < usize(2)) return;
    auto keys = Vec<Key>::with_capacity(values.len());
    for (usize i; i < values.len(); ++i) {
        const T& value = values[i];
        keys.push(key(value));
    }
    auto order         = iter::range(usize(), values.len()).template collect<Vec<usize>>();
    auto observed_keys = keys.as_slice();
    sort_unstable_by(order.as_mut_slice().as_mut_ref(), [&](usize a, usize b) {
        return observed_keys[a] < observed_keys[b] ||
               (! (observed_keys[b] < observed_keys[a]) && a < b);
    });
    apply_slice_order(values, order);
}

} // namespace rstd::slice_

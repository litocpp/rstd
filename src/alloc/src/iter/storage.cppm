export module rstd.alloc:iter.storage;
export import :vec;

template<typename T>
struct IteratorStoredItem {
    using Type                     = T;
    static constexpr bool borrowed = false;
};
template<typename T>
struct IteratorStoredItem<rstd::ref<T>> {
    using Type                     = rstd::ref<T>;
    static constexpr bool borrowed = true;
};
template<typename T>
struct IteratorStoredItem<rstd::mut_ref<T>> {
    using Type                     = rstd::mut_ref<T>;
    static constexpr bool borrowed = true;
};

export namespace rstd::iter
{

template<valid_item T>
using stored_item_t = typename IteratorStoredItem<T>::Type;

template<typename T>
constexpr bool borrowed_item = IteratorStoredItem<T>::borrowed;

template<typename T>
concept storable_item = valid_item<T>;

template<typename T>
    requires storable_item<T>
constexpr auto store_item(T&& value) -> stored_item_t<T> {
    return rstd::forward<T>(value);
}

template<typename Item, typename Key>
constexpr void check_persistent_key() {
    static_assert(! mtp::is_ref<Key>, "persistent keys must be returned by value");
    static_assert(borrowed_item<Item> || ! borrowed_item<Key>,
                  "owning items require independent owning keys");
}

} // namespace rstd::iter

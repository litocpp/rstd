module;
#include <iterator>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <queue>
#include <ranges>
#include <set>
#include <stack>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

export module rstd.cppstd:iter;
import rstd;
import cppstd;

namespace rstd::iter
{

struct StandardIteratorEnd {};

export template<class I>
class IteratorRange : public std::ranges::view_interface<IteratorRange<I>> {
    using Item = typename I::Item;

    I            iterator_;
    Option<Item> item_;

    constexpr void advance() { item_ = iterator_.next(); }

    class Cursor {
        IteratorRange* range_ {};

    public:
        using value_type        = std::remove_cvref_t<Item>;
        using difference_type   = std::ptrdiff_t;
        using iterator_concept  = std::input_iterator_tag;
        using iterator_category = std::input_iterator_tag;

        constexpr Cursor() = default;
        constexpr explicit Cursor(IteratorRange* range): range_(range) {}

        constexpr decltype(auto) operator*() const { return *range_->item_; }

        constexpr auto operator++() -> Cursor& {
            range_->advance();
            return *this;
        }

        constexpr void operator++(int) { ++*this; }

        friend constexpr auto operator==(const Cursor& cursor, StandardIteratorEnd) noexcept
            -> bool {
            return cursor.range_->item_.is_none();
        }

        friend constexpr decltype(auto) iter_move(const Cursor& cursor) {
            return rstd::move(*cursor.range_->item_);
        }
    };

public:
    explicit constexpr IteratorRange(I iterator)
        : iterator_(rstd::move(iterator)), item_(iterator_.next()) {}

    IteratorRange(const IteratorRange&)                    = delete;
    auto operator=(const IteratorRange&) -> IteratorRange& = delete;
    IteratorRange(IteratorRange&&)                         = default;
    auto operator=(IteratorRange&&) -> IteratorRange&      = default;

    constexpr auto begin() -> Cursor { return Cursor(this); }
    constexpr auto end() const -> StandardIteratorEnd { return {}; }
};

export template<has_next I>
constexpr auto as_range(I iterator) -> IteratorRange<I> {
    return IteratorRange<I>(rstd::move(iterator));
}

template<class B>
inline constexpr bool range_double_ended = std::bidirectional_iterator<B>;

template<class B, class E>
inline constexpr bool range_exact_size = std::sized_sentinel_for<E, B>;

export template<class B, class E>
    requires std::input_iterator<B> && std::sentinel_for<E, B>
class IteratorOverRange : public DefaultInClass<IteratorOverRange<B, E>, Iterator> {
    static constexpr bool DOUBLE_ENDED = range_double_ended<B>;
    static constexpr bool EXACT_SIZE   = range_exact_size<B, E>;

    using EndState = std::conditional_t<DOUBLE_ENDED, B, E>;

    static constexpr auto make_end(B begin, E end) -> EndState {
        if constexpr (DOUBLE_ENDED)
            return std::ranges::next(begin, end);
        else
            return end;
    }

    static constexpr auto make_length(const B& begin, const E& end) -> Option<usize> {
        if constexpr (EXACT_SIZE) {
            auto const distance = end - begin;
            return Some(usize(static_cast<rstd::size_t>(distance)));
        } else {
            return None();
        }
    }

    constexpr void decrement_length() {
        if (remaining_.is_some()) --*remaining_;
    }

public:
    using Item = std::iter_reference_t<B>;
    static constexpr bool PROVEN_DOUBLE_ENDED = DOUBLE_ENDED;
    static constexpr bool PROVEN_EXACT_SIZE   = EXACT_SIZE;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = EXACT_SIZE;

    constexpr IteratorOverRange(B begin, E end)
        : front_(begin), end_(make_end(begin, end)), remaining_(make_length(begin, end)) {}

    constexpr auto next() -> Option<Item> {
        if (front_ == end_) return None();
        Item item = *front_;
        ++front_;
        decrement_length();
        return Some<Item>(rstd::forward<Item>(item));
    }

    constexpr auto next_back() -> Option<Item>
        requires DOUBLE_ENDED
    {
        if (front_ == end_) return None();
        --end_;
        decrement_length();
        Item item = *end_;
        return Some<Item>(rstd::forward<Item>(item));
    }

    constexpr auto size_hint() const -> SizeHint {
        if (remaining_.is_some()) {
            auto length = usize(remaining_->to_primitive());
            return { length, Some(length) };
        }
        return { usize(), None() };
    }

    constexpr auto len() const -> usize
        requires EXACT_SIZE
    {
        return *remaining_;
    }

private:
    B             front_;
    EndState      end_;
    Option<usize> remaining_;
};

export template<std::ranges::input_range R>
constexpr auto from_range(R& range [[clang::lifetimebound]]) {
    using Iterator = std::ranges::iterator_t<R>;
    using Sentinel = std::ranges::sentinel_t<R>;
    return IteratorOverRange<Iterator, Sentinel>(std::ranges::begin(range),
                                                  std::ranges::end(range));
}

export template<std::ranges::input_range R>
    requires std::ranges::borrowed_range<R> && (! std::is_lvalue_reference_v<R>)
constexpr auto from_range(R&& range [[clang::lifetimebound]]) {
    using Iterator = std::ranges::iterator_t<R>;
    using Sentinel = std::ranges::sentinel_t<R>;
    return IteratorOverRange<Iterator, Sentinel>(std::ranges::begin(range),
                                                  std::ranges::end(range));
}

} // namespace rstd::iter

namespace rstd::iter::details
{

template<class C, class I>
auto collect_back(I iterator) -> C {
    auto collection = C {};
    if constexpr (requires(C& c, std::size_t size) { c.reserve(size); }) {
        auto const lower = iterator.size_hint().template get<0>().to_primitive();
        collection.reserve(lower);
    }
    for (auto item = iterator.next(); item.is_some(); item = iterator.next())
        collection.push_back(rstd::move(*item));
    return collection;
}

template<class C, class I>
auto collect_forward_list(I iterator) -> C {
    auto collection = C {};
    auto tail       = collection.before_begin();
    for (auto item = iterator.next(); item.is_some(); item = iterator.next())
        tail = collection.insert_after(tail, rstd::move(*item));
    return collection;
}

template<class C, class I>
auto collect_insert(I iterator) -> C {
    auto collection = C {};
    if constexpr (requires(C& c, std::size_t size) { c.reserve(size); }) {
        auto const lower = iterator.size_hint().template get<0>().to_primitive();
        collection.reserve(lower);
    }
    for (auto item = iterator.next(); item.is_some(); item = iterator.next())
        collection.insert(rstd::move(*item));
    return collection;
}

template<class C, class I>
auto collect_map(I iterator) -> C {
    auto collection = C {};
    if constexpr (requires(C& c, std::size_t size) { c.reserve(size); }) {
        auto const lower = iterator.size_hint().template get<0>().to_primitive();
        collection.reserve(lower);
    }
    for (auto item = iterator.next(); item.is_some(); item = iterator.next()) {
        collection.emplace(rstd::move(item->template get<0>()),
                           rstd::move(item->template get<1>()));
    }
    return collection;
}

template<class C, class I>
auto collect_adapter(I iterator) -> C {
    auto collection = C {};
    for (auto item = iterator.next(); item.is_some(); item = iterator.next())
        collection.push(rstd::move(*item));
    return collection;
}

} // namespace rstd::iter::details

export namespace rstd
{

template<class T, class Alloc>
struct Impl<iter::FromIterator<T>, std::vector<T, Alloc>>
    : ImplBase<std::vector<T, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::vector<T, Alloc> {
        return iter::details::collect_back<std::vector<T, Alloc>>(rstd::move(iterator));
    }
};

template<class T, class Alloc>
struct Impl<iter::FromIterator<T>, std::deque<T, Alloc>> : ImplBase<std::deque<T, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::deque<T, Alloc> {
        return iter::details::collect_back<std::deque<T, Alloc>>(rstd::move(iterator));
    }
};

template<class T, class Alloc>
struct Impl<iter::FromIterator<T>, std::list<T, Alloc>> : ImplBase<std::list<T, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::list<T, Alloc> {
        return iter::details::collect_back<std::list<T, Alloc>>(rstd::move(iterator));
    }
};

template<class T, class Alloc>
struct Impl<iter::FromIterator<T>, std::forward_list<T, Alloc>>
    : ImplBase<std::forward_list<T, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::forward_list<T, Alloc> {
        return iter::details::collect_forward_list<std::forward_list<T, Alloc>>(
            rstd::move(iterator));
    }
};

template<class Char, class Traits, class Alloc>
struct Impl<iter::FromIterator<Char>, std::basic_string<Char, Traits, Alloc>>
    : ImplBase<std::basic_string<Char, Traits, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::basic_string<Char, Traits, Alloc> {
        return iter::details::collect_back<std::basic_string<Char, Traits, Alloc>>(
            rstd::move(iterator));
    }
};

template<class T, class Compare, class Alloc>
struct Impl<iter::FromIterator<T>, std::set<T, Compare, Alloc>>
    : ImplBase<std::set<T, Compare, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::set<T, Compare, Alloc> {
        return iter::details::collect_insert<std::set<T, Compare, Alloc>>(rstd::move(iterator));
    }
};

template<class T, class Compare, class Alloc>
struct Impl<iter::FromIterator<T>, std::multiset<T, Compare, Alloc>>
    : ImplBase<std::multiset<T, Compare, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::multiset<T, Compare, Alloc> {
        return iter::details::collect_insert<std::multiset<T, Compare, Alloc>>(
            rstd::move(iterator));
    }
};

template<class T, class Hash, class Equal, class Alloc>
struct Impl<iter::FromIterator<T>, std::unordered_set<T, Hash, Equal, Alloc>>
    : ImplBase<std::unordered_set<T, Hash, Equal, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::unordered_set<T, Hash, Equal, Alloc> {
        return iter::details::collect_insert<std::unordered_set<T, Hash, Equal, Alloc>>(
            rstd::move(iterator));
    }
};

template<class T, class Hash, class Equal, class Alloc>
struct Impl<iter::FromIterator<T>, std::unordered_multiset<T, Hash, Equal, Alloc>>
    : ImplBase<std::unordered_multiset<T, Hash, Equal, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::unordered_multiset<T, Hash, Equal, Alloc> {
        return iter::details::collect_insert<std::unordered_multiset<T, Hash, Equal, Alloc>>(
            rstd::move(iterator));
    }
};

template<class K, class V, class Compare, class Alloc>
struct Impl<iter::FromIterator<tuple<K, V>>, std::map<K, V, Compare, Alloc>>
    : ImplBase<std::map<K, V, Compare, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::map<K, V, Compare, Alloc> {
        return iter::details::collect_map<std::map<K, V, Compare, Alloc>>(rstd::move(iterator));
    }
};

template<class K, class V, class Compare, class Alloc>
struct Impl<iter::FromIterator<tuple<K, V>>, std::multimap<K, V, Compare, Alloc>>
    : ImplBase<std::multimap<K, V, Compare, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::multimap<K, V, Compare, Alloc> {
        return iter::details::collect_map<std::multimap<K, V, Compare, Alloc>>(
            rstd::move(iterator));
    }
};

template<class K, class V, class Hash, class Equal, class Alloc>
struct Impl<iter::FromIterator<tuple<K, V>>, std::unordered_map<K, V, Hash, Equal, Alloc>>
    : ImplBase<std::unordered_map<K, V, Hash, Equal, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::unordered_map<K, V, Hash, Equal, Alloc> {
        return iter::details::collect_map<std::unordered_map<K, V, Hash, Equal, Alloc>>(
            rstd::move(iterator));
    }
};

template<class K, class V, class Hash, class Equal, class Alloc>
struct Impl<iter::FromIterator<tuple<K, V>>,
            std::unordered_multimap<K, V, Hash, Equal, Alloc>>
    : ImplBase<std::unordered_multimap<K, V, Hash, Equal, Alloc>> {
    template<class I>
    static auto from_iter(I iterator) -> std::unordered_multimap<K, V, Hash, Equal, Alloc> {
        return iter::details::collect_map<std::unordered_multimap<K, V, Hash, Equal, Alloc>>(
            rstd::move(iterator));
    }
};

template<class T, class Container>
struct Impl<iter::FromIterator<T>, std::queue<T, Container>>
    : ImplBase<std::queue<T, Container>> {
    template<class I>
    static auto from_iter(I iterator) -> std::queue<T, Container> {
        return iter::details::collect_adapter<std::queue<T, Container>>(rstd::move(iterator));
    }
};

template<class T, class Container>
struct Impl<iter::FromIterator<T>, std::stack<T, Container>>
    : ImplBase<std::stack<T, Container>> {
    template<class I>
    static auto from_iter(I iterator) -> std::stack<T, Container> {
        return iter::details::collect_adapter<std::stack<T, Container>>(rstd::move(iterator));
    }
};

template<class T, class Container, class Compare>
struct Impl<iter::FromIterator<T>, std::priority_queue<T, Container, Compare>>
    : ImplBase<std::priority_queue<T, Container, Compare>> {
    template<class I>
    static auto from_iter(I iterator) -> std::priority_queue<T, Container, Compare> {
        return iter::details::collect_adapter<std::priority_queue<T, Container, Compare>>(
            rstd::move(iterator));
    }
};

} // namespace rstd

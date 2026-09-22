export module rstd.core:iter.traits;
import :num.types;
export import :num.nonzero;
export import :trait;
export import :option;
export import :ops.function;
export import :try_;

namespace rstd::iter
{

// Adapter types live in :iter.adapters; the default-method Impl below needs their
// names to spell its return types, so forward declare them here.
template<class I, class F>
struct Map;
template<class I, class F>
struct MapWhile;
template<class I, class P>
struct Filter;
template<class I, class F>
struct FilterMap;
template<class I>
struct Enumerate;
template<class A, class B>
struct Zip;
template<class I>
struct Take;
template<class I>
struct Skip;
template<class I, class P>
struct TakeWhile;
template<class I, class P>
struct SkipWhile;
template<class I>
struct StepBy;
template<class A, class B>
struct Chain;
template<class I>
struct Rev;
template<class I>
struct Peekable;
template<class I>
struct Flatten;
template<class I, class F>
struct FlatMap;
template<class I>
struct Cloned;
template<class I>
struct Copied;
template<class I, class F>
struct Inspect;
template<class I, class St, class F>
struct Scan;
template<class I>
struct Fuse;
template<class I>
struct Cycle;
template<class I>
struct Intersperse;
template<class I, class F>
struct IntersperseWith;
template<class I>
struct DefaultIfEmpty;
export enum class SingleError { Empty, Multiple };
template<class I>
struct ByRef;
/// `(lower, upper)` bound on the number of remaining elements.
export using SizeHint = rstd::tuple<usize, Option<usize>>;

export struct IteratorEnd {};

/// Iterator items are values or explicit borrow types, never C++ references.
export template<typename T>
concept valid_item = ! mtp::is_ref<T>;

export template<valid_item T>
using checked_item_t = T;

export template<class I>
class IteratorLoop {
    using Item = typename I::Item;

    I*           iterator_;
    Option<Item> item_;

public:
    constexpr explicit IteratorLoop(I& iterator [[clang::lifetimebound]])
        : iterator_(rstd::addressof(iterator)), item_(iterator_->next()) {}

    IteratorLoop(const IteratorLoop&)                    = delete;
    auto operator=(const IteratorLoop&) -> IteratorLoop& = delete;
    constexpr IteratorLoop(IteratorLoop&&)               = default;
    auto operator=(IteratorLoop&&) -> IteratorLoop&      = default;

    friend constexpr auto operator==(const IteratorLoop& loop, IteratorEnd) noexcept -> bool {
        return loop.item_.is_none();
    }

    constexpr void operator++() & { item_ = iterator_->next(); }

    constexpr auto operator*() & -> Item { return item_.take().unwrap_unchecked(); }
};

/// A type is iterable when it exposes the complete `Iterator` required API.
export template<class X>
concept has_next = requires(X& x) {
    typename X::Item;
    requires valid_item<typename X::Item>;
    requires mtp::same_as<decltype(x.next()), Option<typename X::Item>>;
};

/// `Item` of an iterator type.
export template<class X>
using item_of = typename X::Item;

namespace details
{

/// Proof used by allocation-owning collectors. Specializations must always return the same source
/// and each yielded item must satisfy the published source-consumption ratio.
export template<class I>
struct InPlaceTraits {
    using Source = void;

    static constexpr bool  ENABLED   = false;
    static constexpr usize EXPAND_BY = usize();
    static constexpr usize MERGE_BY  = usize();
};

template<class T>
struct is_rstd_borrow {
    static constexpr bool value = false;
};

template<class T>
struct is_rstd_borrow<ref<T>> {
    static constexpr bool value = true;
};

template<class T>
struct is_rstd_borrow<mut_ref<T>> {
    static constexpr bool value = true;
};

template<class T>
concept borrowed_item = is_rstd_borrow<mtp::rm_cvf<T>>::value;

template<class T>
constexpr decltype(auto) observe_item(T&& item) {
    if constexpr (requires { *rstd::forward<T>(item); })
        return *rstd::forward<T>(item);
    else
        return rstd::forward<T>(item);
}

template<class T>
using observed_item_t = mtp::rm_cvf<decltype(observe_item(mtp::declval<T&>()))>;

template<class T>
using aggregate_item_t = mtp::cond<borrowed_item<T>, observed_item_t<T>, mtp::rm_cvf<T>>;

} // namespace details

export template<has_next I>
class IteratorLoopRange {
    I* iterator_;

public:
    constexpr explicit IteratorLoopRange(I& iterator [[clang::lifetimebound]])
        : iterator_(rstd::addressof(iterator)) {}

    constexpr auto begin() & -> IteratorLoop<I> { return IteratorLoop<I>(*iterator_); }
    constexpr auto end() & -> IteratorEnd { return {}; }
};

export template<has_next I>
constexpr auto for_range(I& iterator [[clang::lifetimebound]]) -> IteratorLoopRange<I> {
    return IteratorLoopRange<I>(iterator);
}

/// Trait for types that produce a sequence of values via `next()`.
///
/// Required: `next() -> Option<Item>`. `size_hint()` has a default. Every other
/// method is a provided default (see the `default_tag` Impl below) and can be
/// inherited in-class through `DefaultInClass<Self, Iterator>`.
export struct Iterator {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Iterator;
        using Item  = checked_item_t<typename Self::Item>;

        constexpr auto next() -> Option<Item> { return trait_call<0>(this); }
        constexpr auto size_hint() const -> SizeHint { return trait_call<1>(this); }
    };

    template<class T>
    using Funcs = TraitFuncs<&T::next, &T::size_hint>;
};

/// Build a collection of `A` from an iterator. The actual `from_iter` is a static
/// template on the Impl (generic over the source iterator), so it is not part of
/// `Funcs` and is called directly by `collect`.
export template<typename A>
struct FromIterator {
    using item_t = A;
    template<typename Self, typename = void>
    struct Api {
        using Trait = FromIterator;
    };
};

/// Extend a collection with items yielded by an iterator.
export template<typename A>
struct Extend {
    using item_t = A;
    template<typename Self, typename = void>
    struct Api {
        using Trait = Extend;
    };
};

export template<typename A>
struct Sum {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Sum;
    };
};

export template<typename A>
struct Product {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Product;
    };
};

/// Trait for types convertible into an iterator.
export struct IntoIterator {
    template<typename Self, typename = void>
        requires requires { typename Impl<IntoIterator, mtp::rm_cvf<Self>>::IntoIter; }
    struct Api {
        using Trait    = IntoIterator;
        using IntoIter = typename Impl<IntoIterator, mtp::rm_cvf<Self>>::IntoIter;
        using Item     = checked_item_t<typename IntoIter::Item>;
        constexpr auto into_iter() -> IntoIter { return trait_call<0>(this); }
    };
    template<class T>
    using Funcs = TraitFuncs<&T::into_iter>;
};

export template<typename T>
using into_iter_t = typename Impl<IntoIterator, mtp::rm_cvf<T>>::IntoIter;

export template<typename T>
concept into_iterable = requires { typename Impl<IntoIterator, mtp::rm_cvf<T>>::IntoIter; } &&
                        has_next<typename Impl<IntoIterator, mtp::rm_cvf<T>>::IntoIter>;

export template<typename T>
    requires(! mtp::is_ref<T>) && into_iterable<T>
constexpr auto into_iter(T&& value) -> into_iter_t<T> {
    return as<IntoIterator>(value).into_iter();
}

/// Build `B` from any source that explicitly implements `IntoIterator`.
export template<typename B, typename Source>
    requires(! mtp::is_ref<Source>) && into_iterable<Source> &&
            requires(into_iter_t<Source> iterator) {
                Impl<FromIterator<typename into_iter_t<Source>::Item>, B>::from_iter(
                    rstd::move(iterator));
            }
constexpr auto from_iter(Source&& source) -> B {
    auto iterator = iter::into_iter(rstd::forward<Source>(source));
    using Item    = typename decltype(iterator)::Item;
    return Impl<FromIterator<Item>, B>::from_iter(rstd::move(iterator));
}

/// Extend `collection` from any source that explicitly implements `IntoIterator`.
export template<typename B, typename Source>
    requires(! mtp::is_ref<Source>) && into_iterable<Source> &&
            requires(B& collection, into_iter_t<Source> iterator) {
                Impl<Extend<typename into_iter_t<Source>::Item>, B>::extend(collection,
                                                                            rstd::move(iterator));
            }
constexpr void extend(B& collection, Source&& source) {
    auto iterator = iter::into_iter(rstd::forward<Source>(source));
    using Item    = typename decltype(iterator)::Item;
    Impl<Extend<Item>, B>::extend(collection, rstd::move(iterator));
}

template<typename B, typename A>
    requires requires(B& collection, A&& item) {
        Impl<Extend<mtp::rm_cvf<A>>, B>::extend_one(collection, rstd::forward<A>(item));
    }
constexpr void extend_one(B& collection, A&& item) {
    Impl<Extend<mtp::rm_cvf<A>>, B>::extend_one(collection, rstd::forward<A>(item));
}

/// Iterators that can also yield elements from the back.
export struct DoubleEndedIterator {
    template<typename Self, typename = void>
        requires Impled<Self, Iterator>
    struct Api {
        using Trait = DoubleEndedIterator;
        using Item  = typename Self::Item;
        constexpr auto next_back() -> Option<Item> { return trait_call<0>(this); }
    };
    template<class T>
    using Funcs = TraitFuncs<&T::next_back>;
};

/// Iterators that know their exact remaining length.
export struct ExactSizeIterator {
    template<typename Self, typename = void>
        requires Impled<Self, Iterator>
    struct Api {
        using Trait = ExactSizeIterator;
        constexpr auto len() const -> usize { return trait_call<0>(this); }
        constexpr auto is_empty() const -> bool { return len() == usize(); }
    };
    template<class T>
    using Funcs = TraitFuncs<&T::len>;
};

/// Marker: `next()` keeps returning `None` after the first `None`.
export struct FusedIterator {
    template<typename Self, typename = void>
    struct Api {
        using Trait = FusedIterator;
    };

    template<typename>
    using Funcs = TraitFuncs<>;
};

/// Marker: `size_hint()` reports the exact remaining length, or an overflowed upper bound.
export struct TrustedLen {
    template<typename Self, typename = void>
    struct Api {
        using Trait = TrustedLen;
    };

    template<typename>
    using Funcs = TraitFuncs<>;
};

template<has_next I>
struct IteratorDriver {
    template<typename B, typename F>
    static constexpr auto fold(I& iterator, B init, F& function) -> B {
        B accumulator = rstd::move(init);
        for (auto item = iterator.next(); item.is_some(); item = iterator.next())
            accumulator = function(rstd::move(accumulator), rstd::forward<typename I::Item>(*item));
        return accumulator;
    }

    template<typename B, typename F>
    static constexpr auto try_fold(I& iterator, B init, F& function)
        -> decltype(function(rstd::move(init), rstd::forward<typename I::Item>(*iterator.next()))) {
        using R =
            decltype(function(rstd::move(init), rstd::forward<typename I::Item>(*iterator.next())));
        static_assert(try_::TrySource<R>);
        static_assert(mtp::same_as<try_::output_t<R>, B>);

        B accumulator = rstd::move(init);
        for (auto item = iterator.next(); item.is_some(); item = iterator.next()) {
            auto result = function(rstd::move(accumulator), rstd::forward<typename I::Item>(*item));
            if (! try_::is_success(result)) {
                return try_::from_residual<R>(try_::take_residual(rstd::move(result)));
            }
            accumulator = try_::finish(try_::take_output(rstd::move(result)));
        }
        return try_::from_output<R>(rstd::move(accumulator));
    }
};

template<typename R, has_next I, typename F>
constexpr auto process_successes(I& source, F function) -> R;

template<has_next I, typename B, typename F>
constexpr auto iterator_fold(I& iterator, B init, F& function) -> B {
    return IteratorDriver<I>::fold(iterator, rstd::move(init), function);
}

template<has_next I, typename B, typename F>
constexpr auto iterator_try_fold(I& iterator, B init, F& function) {
    return IteratorDriver<I>::try_fold(iterator, rstd::move(init), function);
}

// Source owners may specialize consumption without bypassing adapter side effects.
template<has_next I>
struct IteratorTraversal {
    static constexpr auto advance(I& iterator, usize n)
        -> Result<empty, num::nonzero::NonZero<usize>> {
        for (usize advanced; advanced < n; ++advanced)
            if (iterator.next().is_none())
                return Err(num::nonzero::NonZero<usize>::make_unchecked(n - advanced));
        return Ok(empty {});
    }
    static constexpr auto count(I& iterator) -> usize {
        usize count;
        while (iterator.next().is_some()) ++count;
        return count;
    }
    static constexpr auto last(I& iterator) -> Option<typename I::Item> {
        auto last = iterator.next();
        if (last.is_none()) return last;
        for (auto item = iterator.next(); item.is_some(); item = iterator.next())
            last = rstd::move(item);
        return last;
    }
};

template<typename I>
    requires Impled<I, DoubleEndedIterator>
constexpr auto double_ended_advance_by(I& iterator, usize n)
    -> Result<empty, num::nonzero::NonZero<usize>> {
    auto backwards = as<DoubleEndedIterator>(iterator);
    for (auto advanced = usize(); advanced < n; ++advanced) {
        if (backwards.next_back().is_none()) {
            return Err(num::nonzero::NonZero<usize>::make_unchecked(n - advanced));
        }
    }
    return Ok(empty {});
}

template<typename I>
    requires Impled<I, DoubleEndedIterator>
constexpr auto double_ended_nth(I& iterator, usize n) -> Option<typename I::Item> {
    auto advanced = double_ended_advance_by(iterator, n);
    if (advanced.is_err()) return None();
    return as<DoubleEndedIterator>(iterator).next_back();
}

template<typename I, typename B, typename F>
    requires Impled<I, DoubleEndedIterator>
constexpr auto double_ended_try_fold(I& iterator, B init, F& function) -> decltype(function(
    rstd::move(init),
    rstd::forward<typename I::Item>(*as<DoubleEndedIterator>(iterator).next_back()))) {
    using R = decltype(function(
        rstd::move(init),
        rstd::forward<typename I::Item>(*as<DoubleEndedIterator>(iterator).next_back())));
    static_assert(try_::TrySource<R>);
    static_assert(mtp::same_as<try_::output_t<R>, B>);

    B    accumulator = rstd::move(init);
    auto backwards   = as<DoubleEndedIterator>(iterator);
    for (auto item = backwards.next_back(); item.is_some(); item = backwards.next_back()) {
        auto result = function(rstd::move(accumulator), rstd::forward<typename I::Item>(*item));
        if (! try_::is_success(result)) {
            return try_::from_residual<R>(try_::take_residual(rstd::move(result)));
        }
        accumulator = try_::finish(try_::take_output(rstd::move(result)));
    }
    return try_::from_output<R>(rstd::move(accumulator));
}

template<typename I, typename B, typename F>
    requires Impled<I, DoubleEndedIterator>
constexpr auto double_ended_fold(I& iterator, B init, F& function) -> B {
    B    accumulator = rstd::move(init);
    auto backwards   = as<DoubleEndedIterator>(iterator);
    for (auto item = backwards.next_back(); item.is_some(); item = backwards.next_back())
        accumulator = function(rstd::move(accumulator), rstd::forward<typename I::Item>(*item));
    return accumulator;
}

template<typename I>
struct ReverseIteratorDriver {
    template<typename B, typename F>
    static constexpr auto fold(I& iterator, B init, F& function) -> B {
        return double_ended_fold(iterator, rstd::move(init), function);
    }
    template<typename B, typename F>
    static constexpr auto try_fold(I& iterator, B init, F& function) {
        return double_ended_try_fold(iterator, rstd::move(init), function);
    }
};

template<typename I, typename Pred>
    requires Impled<I, DoubleEndedIterator>
constexpr auto double_ended_find(I& iterator, Pred& predicate) -> Option<typename I::Item> {
    auto backwards = as<DoubleEndedIterator>(iterator);
    auto item      = backwards.next_back();
    for (; item.is_some(); item = backwards.next_back())
        if (predicate(*item)) break;
    return item;
}

template<typename I, typename Pred>
    requires Impled<I, DoubleEndedIterator> && Impled<I, ExactSizeIterator>
constexpr auto double_ended_position(I& iterator, Pred& predicate) -> Option<usize> {
    usize index     = as<ExactSizeIterator>(iterator).len();
    auto  backwards = as<DoubleEndedIterator>(iterator);
    for (auto item = backwards.next_back(); item.is_some(); item = backwards.next_back()) {
        --index;
        if (predicate(rstd::forward<typename I::Item>(*item))) return Some(index);
    }
    return None();
}

} // namespace rstd::iter

namespace rstd
{

// Generic Iterator Impl: any type with a member next() implements the trait.
// It keeps provided methods on the external Impl path because Impl has priority over in-class.
template<class X>
    requires iter::has_next<X>
struct Impl<iter::Iterator, X> : DefaultInImpl<iter::Iterator, X> {
    constexpr auto next() { return this->self().next(); }
    constexpr auto size_hint() const -> iter::SizeHint {
        if constexpr (requires(const X& x) { x.size_hint(); }) {
            return this->self().size_hint();
        } else {
            return DefaultInImpl<iter::Iterator, X>::size_hint();
        }
    }
};

template<class X>
    requires iter::has_next<X>
struct Impl<iter::IntoIterator, X> : ImplBase<X> {
    using IntoIter = X;

    constexpr auto into_iter() -> IntoIter { return rstd::move(this->self()); }
};

// Capability forwarding is opt-in: the iterator owner must explicitly publish each proof.
template<class X>
    requires iter::has_next<X> && requires { requires X::PROVEN_DOUBLE_ENDED; }
struct Impl<iter::DoubleEndedIterator, X> : ImplBase<X> {
    constexpr auto next_back() { return this->self().next_back(); }
};

template<class X>
    requires iter::has_next<X> && requires { requires X::PROVEN_EXACT_SIZE; }
struct Impl<iter::ExactSizeIterator, X> : ImplBase<X> {
    constexpr auto len() const -> usize { return this->self().len(); }
    constexpr auto is_empty() const -> bool { return len() == usize(); }
};

template<class X>
    requires iter::has_next<X> && requires { requires X::PROVEN_FUSED; }
struct Impl<iter::FusedIterator, X> : ImplBase<X> {};

template<class X>
    requires iter::has_next<X> && requires { requires X::PROVEN_TRUSTED_LEN; }
struct Impl<iter::TrustedLen, X> : ImplBase<X> {};

// All provided Iterator methods. Pulled in-class by DefaultInClass<Self, Iterator>
// so iterators get them as members and chaining needs no as<>().
//
// Item-dependent signatures use deduced `auto` returns: DefaultInClass instantiates
// this Impl while `Self` is still incomplete, so `typename Self::Item` may only appear
// inside method bodies (instantiated lazily on call), never in a member's declaration.
template<typename Tag>
    requires mtp::trait_default_tag<Tag>
struct Impl<iter::Iterator, Tag> : ImplBase<Tag> {
    using Self = mtp::trait_default_self_t<Tag>;

    constexpr auto size_hint() const -> iter::SizeHint { return { usize(), rstd::None() }; }

    constexpr auto begin() & -> iter::IteratorLoop<Self> {
        return iter::IteratorLoop<Self>(this->self());
    }
    constexpr auto end() & -> iter::IteratorEnd { return {}; }

    constexpr auto into_iter() && -> Self { return rstd::move(this->self()); }

    // ---- consuming ----
    constexpr auto count() && -> usize {
        return iter::IteratorTraversal<Self>::count(this->self());
    }

    constexpr auto last() && { return iter::IteratorTraversal<Self>::last(this->self()); }

    constexpr auto nth(usize n) & {
        using Return = Option<typename Self::Item>;
        if (iter::IteratorTraversal<Self>::advance(this->self(), n).is_err()) return Return(None());
        return this->self().next();
    }

    constexpr auto nth(usize n) && { return static_cast<Impl&>(*this).nth(n); }

    template<typename B, typename F>
    constexpr auto fold(B init, F f) && -> B {
        return iter::iterator_fold(this->self(), rstd::move(init), f);
    }

    template<typename F>
    constexpr auto reduce(F f) && {
        auto first = this->self().next();
        if (first.is_none()) return first;
        auto acc = rstd::move(first);
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            acc = rstd::Some<typename Self::Item>(f(rstd::forward<typename Self::Item>(*acc),
                                                    rstd::forward<typename Self::Item>(*x)));
        return acc;
    }

    template<typename F>
    constexpr void for_each(F f) && {
        auto step = [&f](empty, typename Self::Item item) {
            f(rstd::forward<typename Self::Item>(item));
            return empty {};
        };
        (void)iter::iterator_fold(this->self(), empty {}, step);
    }

    template<typename S = void>
    constexpr auto sum() && {
        using Target = mtp::
            cond<mtp::same_as<S, void>, iter::details::aggregate_item_t<typename Self::Item>, S>;
        return Impl<iter::Sum<typename Self::Item>, Target>::sum(rstd::move(this->self()));
    }

    template<typename P = void>
    constexpr auto product() && {
        using Target = mtp::
            cond<mtp::same_as<P, void>, iter::details::aggregate_item_t<typename Self::Item>, P>;
        return Impl<iter::Product<typename Self::Item>, Target>::product(rstd::move(this->self()));
    }

    template<typename Pred>
    constexpr auto all(Pred pred) & -> bool {
        auto step = [&pred](empty, typename Self::Item item) {
            if (pred(rstd::forward<typename Self::Item>(item)))
                return ops::ControlFlow<bool, empty>::Continue();
            else
                return ops::ControlFlow<bool, empty>::Break(false);
        };
        return iter::iterator_try_fold(this->self(), empty {}, step).is_continue();
    }

    template<typename Pred>
    constexpr auto all(Pred pred) && -> bool {
        return static_cast<Impl&>(*this).all(rstd::move(pred));
    }

    template<typename Pred>
    constexpr auto any(Pred pred) & -> bool {
        auto step = [&pred](empty, typename Self::Item item) {
            if (pred(rstd::forward<typename Self::Item>(item)))
                return ops::ControlFlow<bool, empty>::Break(true);
            else
                return ops::ControlFlow<bool, empty>::Continue();
        };
        return iter::iterator_try_fold(this->self(), empty {}, step).is_break();
    }

    template<typename Pred>
    constexpr auto any(Pred pred) && -> bool {
        return static_cast<Impl&>(*this).any(rstd::move(pred));
    }

    template<typename Pred>
    constexpr auto find(Pred pred) & {
        auto x = this->self().next();
        for (; x.is_some(); x = this->self().next())
            if (pred(*x)) break;
        return x;
    }

    template<typename Pred>
    constexpr auto find(Pred pred) && {
        return static_cast<Impl&>(*this).find(rstd::move(pred));
    }

    template<typename F>
    constexpr auto find_map(F f) & {
        using R = decltype(f(rstd::forward<typename Self::Item>(*this->self().next())));
        R r;
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            r = f(rstd::forward<typename Self::Item>(*x));
            if (r.is_some()) return r;
        }
        return r;
    }

    template<typename F>
    constexpr auto find_map(F f) && {
        return static_cast<Impl&>(*this).find_map(rstd::move(f));
    }

    template<typename Pred>
    constexpr auto position(Pred pred) & -> Option<usize> {
        usize i;
        for (auto x = this->self().next(); x.is_some(); x = this->self().next(), ++i)
            if (pred(rstd::forward<typename Self::Item>(*x))) return rstd::Some(i);
        return rstd::None();
    }

    template<typename Pred>
    constexpr auto position(Pred pred) && -> Option<usize> {
        return static_cast<Impl&>(*this).position(rstd::move(pred));
    }

    constexpr auto min() && {
        auto best = this->self().next();
        if (best.is_none()) return best;
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            if (*x < *best) best = rstd::move(x);
        return best;
    }

    constexpr auto max() && {
        auto best = this->self().next();
        if (best.is_none()) return best;
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            if (! (*x < *best)) best = rstd::move(x);
        return best;
    }

    template<typename B>
    constexpr auto collect() && -> B {
        return iter::from_iter<B>(rstd::move(this->self()));
    }

    template<typename S = Self>
    constexpr auto single() && -> Result<typename S::Item, iter::SingleError> {
        auto first = this->self().next();
        if (first.is_none()) return Err(iter::SingleError::Empty);
        if (this->self().next().is_some()) return Err(iter::SingleError::Multiple);
        return Ok<typename S::Item>(rstd::forward<typename S::Item>(*first));
    }

    template<typename S = Self>
    constexpr auto default_if_empty(typename S::Item fallback) && -> iter::DefaultIfEmpty<Self> {
        return iter::DefaultIfEmpty<Self>(rstd::move(this->self()),
                                          rstd::forward<typename S::Item>(fallback));
    }

    template<typename B>
    constexpr auto try_collect() & {
        using R = try_::change_output_t<typename Self::Item, B>;
        return iter::process_successes<R>(this->self(), [](auto values) {
            return iter::from_iter<B>(rstd::move(values));
        });
    }

    template<typename B>
    constexpr auto try_collect() && {
        return static_cast<Impl&>(*this).template try_collect<B>();
    }

    template<typename B>
    constexpr auto collect_into(B& collection [[clang::lifetimebound]]) && -> B& {
        iter::extend(collection, rstd::move(this->self()));
        return collection;
    }

    template<typename Pred>
    constexpr auto is_partitioned(Pred predicate) && -> bool {
        bool tail = false;
        for (auto item = this->self().next(); item.is_some(); item = this->self().next()) {
            if (predicate(rstd::forward<typename Self::Item>(*item))) {
                if (tail) return false;
            } else {
                tail = true;
            }
        }
        return true;
    }

    template<typename F>
    constexpr auto try_find(F predicate) & {
        using Item = typename Self::Item;
        using R    = decltype(predicate(mtp::declval<const mtp::rm_ref<Item>&>()));
        static_assert(mtp::same_as<try_::output_t<R>, bool>);
        using Found = try_::change_output_t<R, Option<Item>>;
        for (auto item = this->self().next(); item.is_some(); item = this->self().next()) {
            const auto& observed = *item;
            auto        result   = predicate(observed);
            if (! try_::is_success(result))
                return try_::from_residual<Found>(try_::take_residual(rstd::move(result)));
            if (try_::finish(try_::take_output(rstd::move(result))))
                return try_::from_output<Found>(rstd::move(item));
        }
        return try_::from_output<Found>(Option<Item>(None()));
    }

    template<typename F>
    constexpr auto try_find(F predicate) && {
        return static_cast<Impl&>(*this).try_find(rstd::move(predicate));
    }

    template<typename F>
    constexpr auto try_reduce(F function) & {
        using Item = typename Self::Item;
        using R    = decltype(function(mtp::declval<Item>(), mtp::declval<Item>()));
        static_assert(mtp::same_as<try_::output_t<R>, Item>);
        using Reduced = try_::change_output_t<R, Option<Item>>;
        auto first    = this->self().next();
        if (first.is_none()) return try_::from_output<Reduced>(rstd::move(first));
        auto step = [&function](Option<Item> accumulator, Item item) -> Reduced {
            auto result = function(rstd::forward<Item>(*accumulator), rstd::forward<Item>(item));
            if (! try_::is_success(result))
                return try_::from_residual<Reduced>(try_::take_residual(rstd::move(result)));
            return try_::from_output<Reduced>(
                Some<Item>(try_::finish(try_::take_output(rstd::move(result)))));
        };
        return iter::iterator_try_fold(this->self(), rstd::move(first), step);
    }

    template<typename F>
    constexpr auto try_reduce(F function) && {
        return static_cast<Impl&>(*this).try_reduce(rstd::move(function));
    }

    template<typename B, typename F>
    constexpr auto try_fold(B init, F f) & {
        return iter::iterator_try_fold(this->self(), rstd::move(init), f);
    }

    template<typename B, typename F>
    constexpr auto try_fold(B init, F f) && {
        return static_cast<Impl&>(*this).try_fold(rstd::move(init), rstd::move(f));
    }

    template<typename F>
    constexpr auto try_for_each(F f) & {
        auto step = [&f](empty, typename Self::Item item) {
            return f(rstd::forward<typename Self::Item>(item));
        };
        return iter::iterator_try_fold(this->self(), empty {}, step);
    }

    template<typename F>
    constexpr auto try_for_each(F f) && {
        return static_cast<Impl&>(*this).try_for_each(rstd::move(f));
    }

    template<typename F>
    constexpr auto min_by_key(F f) && {
        auto best = this->self().next();
        if (best.is_none()) return best;
        auto bestk = f(*best);
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            auto k = f(*x);
            if (k < bestk) {
                bestk = rstd::move(k);
                best  = rstd::move(x);
            }
        }
        return best;
    }

    template<typename F>
    constexpr auto max_by_key(F f) && {
        auto best = this->self().next();
        if (best.is_none()) return best;
        auto bestk = f(*best);
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            auto k = f(*x);
            if (! (k < bestk)) {
                bestk = rstd::move(k);
                best  = rstd::move(x);
            }
        }
        return best;
    }

    // Lexicographic equality against another IntoIterator source.
    template<typename U>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto eq(U&& source) && -> bool {
        return static_cast<Impl&&>(*this).eq_by(rstd::forward<U>(source),
                                                [](const auto& a, const auto& b) {
                                                    return a == b;
                                                });
    }

    template<typename U, typename F>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto eq_by(U&& source, F equal) && -> bool {
        auto other = iter::into_iter(rstd::forward<U>(source));
        for (;;) {
            auto a = this->self().next();
            auto b = other.next();
            if (a.is_none() || b.is_none()) return a.is_none() && b.is_none();
            if (! equal(rstd::forward<typename Self::Item>(*a),
                        rstd::forward<typename decltype(other)::Item>(*b)))
                return false;
        }
    }

    template<typename U, typename F>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto ne_by(U&& source, F equal) && -> bool {
        return ! static_cast<Impl&&>(*this).eq_by(rstd::forward<U>(source), rstd::move(equal));
    }

    template<typename U>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto ne(U&& other) && -> bool {
        return ! static_cast<Impl&&>(*this).eq(rstd::forward<U>(other));
    }

    // Lexicographic ordering against another IntoIterator source.
    template<typename U, class S = Self>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U> &&
                requires(const typename S::Item& a, const typename iter::into_iter_t<U>::Item& b) {
                    { a <=> b } -> mtp::same_as<rstd::strong_ordering>;
                }
    constexpr auto cmp(U&& source) && -> rstd::strong_ordering {
        return static_cast<Impl&&>(*this).cmp_by(
            rstd::forward<U>(source), [](const auto& a, const auto& b) -> rstd::strong_ordering {
                return a <=> b;
            });
    }

    template<typename U, typename F>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto cmp_by(U&& source, F compare) && -> rstd::strong_ordering {
        auto other = iter::into_iter(rstd::forward<U>(source));
        for (;;) {
            auto a = this->self().next();
            auto b = other.next();
            if (a.is_none() && b.is_none()) return rstd::strong_ordering::equal;
            if (a.is_none()) return rstd::strong_ordering::less;
            if (b.is_none()) return rstd::strong_ordering::greater;
            auto order = compare(rstd::forward<typename Self::Item>(*a),
                                 rstd::forward<typename decltype(other)::Item>(*b));
            static_assert(mtp::same_as<decltype(order), rstd::strong_ordering>);
            if (order != 0) return order;
        }
    }

    template<typename U>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto partial_cmp(U&& source) && -> rstd::partial_ordering {
        return static_cast<Impl&&>(*this).partial_cmp_by(
            rstd::forward<U>(source), [](const auto& a, const auto& b) -> rstd::partial_ordering {
                return a <=> b;
            });
    }

    template<typename U, typename F>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto partial_cmp_by(U&& source, F compare) && -> rstd::partial_ordering {
        auto other = iter::into_iter(rstd::forward<U>(source));
        for (;;) {
            auto a = this->self().next();
            auto b = other.next();
            if (a.is_none() && b.is_none()) return rstd::partial_ordering::equivalent;
            if (a.is_none()) return rstd::partial_ordering::less;
            if (b.is_none()) return rstd::partial_ordering::greater;
            rstd::partial_ordering order =
                compare(rstd::forward<typename Self::Item>(*a),
                        rstd::forward<typename decltype(other)::Item>(*b));
            if (order != 0) return order;
        }
    }

    template<typename U>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto lt(U&& other) && -> bool {
        return static_cast<Impl&&>(*this).partial_cmp(rstd::forward<U>(other)) < 0;
    }
    template<typename U>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto le(U&& other) && -> bool {
        return static_cast<Impl&&>(*this).partial_cmp(rstd::forward<U>(other)) <= 0;
    }
    template<typename U>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto gt(U&& other) && -> bool {
        return static_cast<Impl&&>(*this).partial_cmp(rstd::forward<U>(other)) > 0;
    }
    template<typename U>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto ge(U&& other) && -> bool {
        return static_cast<Impl&&>(*this).partial_cmp(rstd::forward<U>(other)) >= 0;
    }

    // compare(a, b) -> strong_ordering; returns the minimum (first on ties).
    template<typename F>
    constexpr auto min_by(F compare) && {
        auto best = this->self().next();
        if (best.is_none()) return best;
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            if (compare(*best, *x) == rstd::strong_ordering::greater) best = rstd::move(x);
        return best;
    }

    // compare(a, b) -> strong_ordering; returns the maximum (last on ties).
    template<typename F>
    constexpr auto max_by(F compare) && {
        auto best = this->self().next();
        if (best.is_none()) return best;
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            if (compare(*best, *x) != rstd::strong_ordering::greater) best = rstd::move(x);
        return best;
    }

    // Index (from the front) of the last element matching `pred`.
    // Requires DoubleEndedIterator + ExactSizeIterator.
    template<typename Pred>
        requires Impled<Self, iter::DoubleEndedIterator> && Impled<Self, iter::ExactSizeIterator>
    constexpr auto rposition(Pred pred) & -> Option<usize> {
        return iter::double_ended_position(this->self(), pred);
    }

    template<typename Pred>
        requires Impled<Self, iter::DoubleEndedIterator> && Impled<Self, iter::ExactSizeIterator>
    constexpr auto rposition(Pred pred) && -> Option<usize> {
        return static_cast<Impl&>(*this).rposition(rstd::move(pred));
    }

    constexpr auto advance_back_by(usize n) & -> Result<empty, num::nonzero::NonZero<usize>>
        requires Impled<Self, iter::DoubleEndedIterator>
    {
        return iter::double_ended_advance_by(this->self(), n);
    }

    constexpr auto advance_back_by(usize n) && -> Result<empty, num::nonzero::NonZero<usize>>
        requires Impled<Self, iter::DoubleEndedIterator>
    {
        return static_cast<Impl&>(*this).advance_back_by(n);
    }

    constexpr auto nth_back(usize n) &
        requires Impled<Self, iter::DoubleEndedIterator>
    {
        return iter::double_ended_nth(this->self(), n);
    }

    constexpr auto nth_back(usize n) &&
        requires Impled<Self, iter::DoubleEndedIterator>
    {
        return static_cast<Impl&>(*this).nth_back(n);
    }

    template<typename B, typename F>
    constexpr auto try_rfold(B init, F f) &
        requires Impled<Self, iter::DoubleEndedIterator>
    {
        return iter::ReverseIteratorDriver<Self>::try_fold(this->self(), rstd::move(init), f);
    }

    template<typename B, typename F>
    constexpr auto try_rfold(B init, F f) &&
        requires Impled<Self, iter::DoubleEndedIterator>
    {
        return static_cast<Impl&>(*this).try_rfold(rstd::move(init), rstd::move(f));
    }

    template<typename B, typename F>
    constexpr auto rfold(B init, F f) && -> B
        requires Impled<Self, iter::DoubleEndedIterator>
    {
        return iter::ReverseIteratorDriver<Self>::fold(this->self(), rstd::move(init), f);
    }

    template<typename Pred>
    constexpr auto rfind(Pred pred) &
        requires Impled<Self, iter::DoubleEndedIterator>
    {
        return iter::double_ended_find(this->self(), pred);
    }

    template<typename Pred>
    constexpr auto rfind(Pred pred) &&
        requires Impled<Self, iter::DoubleEndedIterator>
    {
        return static_cast<Impl&>(*this).rfind(rstd::move(pred));
    }

    constexpr auto advance_by(usize n) & -> Result<empty, num::nonzero::NonZero<usize>> {
        return iter::IteratorTraversal<Self>::advance(this->self(), n);
    }

    constexpr auto advance_by(usize n) && -> Result<empty, num::nonzero::NonZero<usize>> {
        return static_cast<Impl&>(*this).advance_by(n);
    }

    constexpr auto is_empty() const& -> bool
        requires Impled<Self, iter::ExactSizeIterator>
    {
        return as<iter::ExactSizeIterator>(this->self()).len() == usize();
    }

    constexpr auto is_sorted() && -> bool {
        return static_cast<Impl&&>(*this).is_sorted_by([](const auto& a, const auto& b) {
            return a <= b;
        });
    }

    template<typename F>
    constexpr auto is_sorted_by(F ordered) && -> bool {
        auto prev = this->self().next();
        if (prev.is_none()) return true;
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            if (! ordered(*prev, *x)) return false;
            prev = rstd::move(x);
        }
        return true;
    }

    // key(item) -> comparable; checks the keys are non-decreasing.
    template<typename F>
    constexpr auto is_sorted_by_key(F key) && -> bool {
        auto first = this->self().next();
        if (first.is_none()) return true;
        auto prev = key(*first);
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            auto k = key(*x);
            if (k < prev) return false;
            prev = rstd::move(k);
        }
        return true;
    }

    template<typename B, typename Pred>
    constexpr auto partition(Pred pred) && -> rstd::tuple<B, B> {
        B yes {};
        B no {};
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            if (pred(*x))
                iter::extend_one(yes, rstd::forward<typename Self::Item>(*x));
            else
                iter::extend_one(no, rstd::forward<typename Self::Item>(*x));
        }
        return { rstd::move(yes), rstd::move(no) };
    }

    template<typename CA, typename CB>
    constexpr auto unzip() && -> rstd::tuple<CA, CB> {
        CA a {};
        CB b {};
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            iter::extend_one(a, rstd::move(x->template get<0>()));
            iter::extend_one(b, rstd::move(x->template get<1>()));
        }
        return { rstd::move(a), rstd::move(b) };
    }

    // Borrows the iterator so adapters can consume from it without moving it.
    constexpr auto by_ref() & [[clang::lifetimebound]] -> iter::ByRef<Self> {
        return iter::ByRef<Self>(rstd::addressof(this->self()));
    }

    auto by_ref() && -> iter::ByRef<Self> = delete;

    // ---- adapters (move the receiver into the adapter) ----
    template<typename F>
    constexpr auto map(F f) && -> iter::Map<Self, F> {
        return iter::Map<Self, F>(rstd::move(this->self()), rstd::move(f));
    }

    template<typename F>
    constexpr auto map_while(F f) && -> iter::MapWhile<Self, F> {
        return iter::MapWhile<Self, F>(rstd::move(this->self()), rstd::move(f));
    }

    template<typename Pred>
    constexpr auto filter(Pred pred) && -> iter::Filter<Self, Pred> {
        return iter::Filter<Self, Pred>(rstd::move(this->self()), rstd::move(pred));
    }

    template<typename F>
    constexpr auto filter_map(F f) && -> iter::FilterMap<Self, F> {
        return iter::FilterMap<Self, F>(rstd::move(this->self()), rstd::move(f));
    }

    constexpr auto enumerate() && -> iter::Enumerate<Self> {
        return iter::Enumerate<Self>(rstd::move(this->self()));
    }

    template<typename U>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U>
    constexpr auto zip(U&& source) && -> iter::Zip<Self, iter::into_iter_t<U>> {
        auto other = iter::into_iter(rstd::forward<U>(source));
        return iter::Zip<Self, decltype(other)>(rstd::move(this->self()), rstd::move(other));
    }

    constexpr auto take(usize n) && -> iter::Take<Self> {
        return iter::Take<Self>(rstd::move(this->self()), n);
    }

    constexpr auto skip(usize n) && -> iter::Skip<Self> {
        return iter::Skip<Self>(rstd::move(this->self()), n);
    }

    constexpr auto step_by(usize step) && -> iter::StepBy<Self> {
        if (step == usize()) rstd::panic("step_by called with step 0");
        return iter::StepBy<Self>(rstd::move(this->self()), step);
    }

    template<typename Pred>
    constexpr auto take_while(Pred p) && -> iter::TakeWhile<Self, Pred> {
        return iter::TakeWhile<Self, Pred>(rstd::move(this->self()), rstd::move(p));
    }

    template<typename Pred>
    constexpr auto skip_while(Pred p) && -> iter::SkipWhile<Self, Pred> {
        return iter::SkipWhile<Self, Pred>(rstd::move(this->self()), rstd::move(p));
    }

    template<typename U, class S = Self>
        requires(! mtp::is_ref<U>) && iter::into_iterable<U> &&
                mtp::same_as<typename iter::into_iter_t<U>::Item, typename S::Item>
    constexpr auto chain(U&& source) && -> iter::Chain<Self, iter::into_iter_t<U>> {
        auto other = iter::into_iter(rstd::forward<U>(source));
        return iter::Chain<Self, decltype(other)>(rstd::move(this->self()), rstd::move(other));
    }

    constexpr auto rev() && -> iter::Rev<Self>
        requires Impled<Self, iter::DoubleEndedIterator>
    {
        return iter::Rev<Self>(rstd::move(this->self()));
    }

    constexpr auto peekable() && -> iter::Peekable<Self> {
        return iter::Peekable<Self>(rstd::move(this->self()));
    }

    template<class S = Self>
        requires(! mtp::is_ref<typename S::Item>) && iter::into_iterable<typename S::Item>
    constexpr auto flatten() && -> iter::Flatten<Self> {
        return iter::Flatten<Self>(rstd::move(this->self()));
    }

    template<typename F, class S = Self, class Mapped = mtp::invoke_result_t<F&, typename S::Item>>
        requires(! mtp::is_ref<Mapped>) && iter::into_iterable<Mapped>
    constexpr auto flat_map(F f) && -> iter::FlatMap<Self, F> {
        return iter::FlatMap<Self, F>(rstd::move(this->self()), rstd::move(f));
    }

    template<class S = Self>
        requires iter::details::borrowed_item<typename S::Item> &&
                 Impled<iter::details::observed_item_t<typename S::Item>, clone::Clone>
    constexpr auto cloned() && -> iter::Cloned<Self> {
        return iter::Cloned<Self>(rstd::move(this->self()));
    }

    template<class S = Self>
        requires iter::details::borrowed_item<typename S::Item> &&
                 mtp::copy<iter::details::observed_item_t<typename S::Item>>
    constexpr auto copied() && -> iter::Copied<Self> {
        return iter::Copied<Self>(rstd::move(this->self()));
    }

    template<typename F>
    constexpr auto inspect(F f) && -> iter::Inspect<Self, F> {
        return iter::Inspect<Self, F>(rstd::move(this->self()), rstd::move(f));
    }

    template<typename St, typename F>
    constexpr auto scan(St st, F f) && -> iter::Scan<Self, St, F> {
        return iter::Scan<Self, St, F>(rstd::move(this->self()), rstd::move(st), rstd::move(f));
    }

    constexpr auto fuse() && -> iter::Fuse<Self> {
        return iter::Fuse<Self>(rstd::move(this->self()));
    }

    constexpr auto cycle() && -> iter::Cycle<Self>
        requires Impled<Self, clone::Clone>
    {
        return iter::Cycle<Self>(rstd::move(this->self()));
    }

    template<class S = Self>
        requires Impled<typename S::Item, clone::Clone>
    constexpr auto intersperse(typename S::Item sep) && -> iter::Intersperse<Self> {
        return iter::Intersperse<Self>(rstd::move(this->self()), rstd::move(sep));
    }

    template<typename F>
    constexpr auto intersperse_with(F factory) && -> iter::IntersperseWith<Self, F> {
        return iter::IntersperseWith<Self, F>(rstd::move(this->self()), rstd::move(factory));
    }
};

} // namespace rstd

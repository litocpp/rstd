export module rstd.core:iter.traits;
export import :trait;
export import :option;
export import :ops.function;

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
template<class I>
struct ByRef;

/// `(lower, upper)` bound on the number of remaining elements.
export using SizeHint = rstd::tuple<usize, Option<usize>>;

/// A type is iterable when it exposes a `next()` member returning an `Option`.
export template<class X>
concept has_next = requires(X& x) { x.next(); };

/// `Item` of an iterator type, derived from its `next()` return.
export template<class X>
using item_of = typename decltype(mtp::declval<X&>().next())::value_type;

/// Trait for types that produce a sequence of values via `next()`.
///
/// Required: `next() -> Option<Item>`. `size_hint()` has a default. Every other
/// method is a provided default (see the `default_tag` Impl below) and can be
/// inherited in-class through `DefaultInClass<Self, Iterator>`.
export struct Iterator {
    template<typename Self, typename = void>
    struct RequiredApi {
        using Trait = Iterator;
        using Item  = typename Self::Item;

        auto next() -> Option<Item> { return trait_required_call<0>(this); }
    };

    template<typename Self, typename = void>
    struct Api {
        using Trait = Iterator;
        using Item  = typename Self::Item;

        auto next() -> Option<Item> { return trait_call<0>(this); }
        auto size_hint() const -> SizeHint { return trait_call<1>(this); }
    };

    template<class T>
    using RequiredFuncs = TraitFuncs<&T::next>;

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

/// Trait for types convertible into an iterator.
export struct IntoIterator {
    template<typename Self, typename = void>
    struct Api {
        using Trait    = IntoIterator;
        using IntoIter = typename Self::IntoIter;
        using Item     = typename IntoIter::Item;
        auto into_iter() -> IntoIter { return trait_call<0>(this); }
    };
    template<class T>
    using Funcs = TraitFuncs<&T::into_iter>;
};

/// Iterators that can also yield elements from the back.
export struct DoubleEndedIterator {
    template<typename Self, typename = void>
        requires Impled<Self, Iterator>
    struct Api {
        using Trait = DoubleEndedIterator;
        using Item  = typename Self::Item;
        auto next_back() -> Option<Item> { return trait_call<0>(this); }
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
        auto len() const -> usize { return trait_call<0>(this); }
    };
    template<class T>
    using Funcs = TraitFuncs<&T::len>;
};

/// Marker: `next()` keeps returning `None` after the first `None`.
export struct FusedIterator {
    static constexpr bool direct { true };
    template<typename Self, typename = void>
    struct Api {
        using Trait = FusedIterator;
    };

    template<typename>
    using Funcs = TraitFuncs<>;
};

} // namespace rstd::iter

namespace rstd
{

// Generic Iterator Impl: any type with a member next() implements the trait.
// It keeps provided methods on the external Impl path because Impl has priority over in-class.
template<class X>
    requires iter::has_next<X>
struct Impl<iter::Iterator, X> : DefaultInImpl<iter::Iterator, X> {
    auto next() { return this->self().next(); }
    auto size_hint() const -> iter::SizeHint {
        if constexpr (requires(const X& x) { x.size_hint(); }) {
            return this->self().size_hint();
        } else {
            return DefaultInImpl<iter::Iterator, X>::size_hint();
        }
    }
};

// Generic DoubleEnded/ExactSize Impls for iterators exposing next_back()/len().
template<class X>
    requires iter::has_next<X> && requires(X& x) { x.next_back(); }
struct Impl<iter::DoubleEndedIterator, X> : ImplBase<X> {
    auto next_back() { return this->self().next_back(); }
};

template<class X>
    requires iter::has_next<X> && requires(const X& x) { x.len(); }
struct Impl<iter::ExactSizeIterator, X> : ImplBase<X> {
    auto len() const -> usize { return this->self().len(); }
};

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

    auto size_hint() const -> iter::SizeHint { return { 0, rstd::None() }; }

    // ---- consuming ----
    auto count() -> usize {
        usize n = 0;
        while (this->self().next().is_some()) ++n;
        return n;
    }

    auto last() {
        auto out = this->self().next();
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            out = rstd::move(x);
        return out;
    }

    auto nth(usize n) {
        auto x = this->self().next();
        for (usize i = 0; i < n && x.is_some(); ++i)
            x = this->self().next();
        return x;
    }

    template<typename B, typename F>
    auto fold(B init, F f) -> B {
        B acc = rstd::move(init);
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            acc = f(rstd::move(acc), rstd::move(*x));
        return acc;
    }

    template<typename F>
    auto reduce(F f) {
        auto first = this->self().next();
        if (first.is_none()) return first;
        auto acc = rstd::move(*first);
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            acc = f(rstd::move(acc), rstd::move(*x));
        return rstd::Some(rstd::move(acc));
    }

    template<typename F>
    void for_each(F f) {
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            f(rstd::move(*x));
    }

    auto sum() {
        typename Self::Item acc {};
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            acc = acc + rstd::move(*x);
        return acc;
    }

    auto product() {
        typename Self::Item acc { 1 };
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            acc = acc * rstd::move(*x);
        return acc;
    }

    template<typename Pred>
    auto all(Pred pred) -> bool {
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            if (! pred(*x)) return false;
        return true;
    }

    template<typename Pred>
    auto any(Pred pred) -> bool {
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            if (pred(*x)) return true;
        return false;
    }

    template<typename Pred>
    auto find(Pred pred) {
        auto x = this->self().next();
        for (; x.is_some(); x = this->self().next())
            if (pred(*x)) break;
        return x;
    }

    template<typename F>
    auto find_map(F f) {
        using R = decltype(f(rstd::move(*this->self().next())));
        R r;
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            r = f(rstd::move(*x));
            if (r.is_some()) return r;
        }
        return r;
    }

    template<typename Pred>
    auto position(Pred pred) -> Option<usize> {
        usize i = 0;
        for (auto x = this->self().next(); x.is_some(); x = this->self().next(), ++i)
            if (pred(*x)) return rstd::Some(i);
        return rstd::None();
    }

    auto min() {
        auto best = this->self().next();
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            if (*x < *best) best = rstd::move(x);
        return best;
    }

    auto max() {
        auto best = this->self().next();
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            if (! (*x < *best)) best = rstd::move(x);
        return best;
    }

    template<typename B>
    auto collect() -> B {
        using Item = typename Self::Item;
        return Impl<iter::FromIterator<Item>, B>::from_iter(rstd::move(this->self()));
    }

    // f returns Option<B>; a None stops the fold and yields None.
    template<typename B, typename F>
    auto try_fold(B init, F f) -> Option<B> {
        B acc = rstd::move(init);
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            auto r = f(rstd::move(acc), rstd::move(*x));
            if (r.is_none()) return rstd::None();
            acc = rstd::move(*r);
        }
        return rstd::Some(rstd::move(acc));
    }

    // f returns Option<unit-ish>; a None stops early and yields None.
    template<typename F>
    auto try_for_each(F f) {
        using R = decltype(f(*this->self().next()));
        R ok    = rstd::None();
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            ok = f(*x);
            if (ok.is_none()) return ok;
        }
        return ok;
    }

    template<typename F>
    auto min_by_key(F f) {
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
    auto max_by_key(F f) {
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

    // Lexicographic equality against another iterator.
    template<typename U>
    auto eq(U other) -> bool {
        for (;;) {
            auto a = this->self().next();
            auto b = other.next();
            if (a.is_none() || b.is_none()) return a.is_none() && b.is_none();
            if (! (*a == *b)) return false;
        }
    }

    template<typename U>
    auto ne(U other) -> bool {
        return ! this->self().eq(rstd::move(other));
    }

    // Lexicographic ordering against another iterator.
    template<typename U>
    auto cmp(U other) -> rstd::strong_ordering {
        for (;;) {
            auto a = this->self().next();
            auto b = other.next();
            if (a.is_none() && b.is_none()) return rstd::strong_ordering::equal;
            if (a.is_none()) return rstd::strong_ordering::less;
            if (b.is_none()) return rstd::strong_ordering::greater;
            if (*a < *b) return rstd::strong_ordering::less;
            if (*b < *a) return rstd::strong_ordering::greater;
        }
    }

    template<typename U>
    auto lt(U other) -> bool {
        return this->self().cmp(rstd::move(other)) == rstd::strong_ordering::less;
    }
    template<typename U>
    auto le(U other) -> bool {
        return this->self().cmp(rstd::move(other)) != rstd::strong_ordering::greater;
    }
    template<typename U>
    auto gt(U other) -> bool {
        return this->self().cmp(rstd::move(other)) == rstd::strong_ordering::greater;
    }
    template<typename U>
    auto ge(U other) -> bool {
        return this->self().cmp(rstd::move(other)) != rstd::strong_ordering::less;
    }

    // compare(a, b) -> strong_ordering; returns the minimum (first on ties).
    template<typename F>
    auto min_by(F compare) {
        auto best = this->self().next();
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            if (compare(*best, *x) == rstd::strong_ordering::greater) best = rstd::move(x);
        return best;
    }

    // compare(a, b) -> strong_ordering; returns the maximum (last on ties).
    template<typename F>
    auto max_by(F compare) {
        auto best = this->self().next();
        for (auto x = this->self().next(); x.is_some(); x = this->self().next())
            if (compare(*best, *x) != rstd::strong_ordering::greater) best = rstd::move(x);
        return best;
    }

    // Index (from the front) of the last element matching `pred`.
    // Requires DoubleEndedIterator + ExactSizeIterator.
    template<typename Pred>
    auto rposition(Pred pred) -> Option<usize> {
        usize i = this->self().len();
        for (auto x = this->self().next_back(); x.is_some(); x = this->self().next_back()) {
            --i;
            if (pred(*x)) return rstd::Some(i);
        }
        return rstd::None();
    }

    // The n-th element from the back. Requires DoubleEndedIterator.
    auto nth_back(usize n) {
        auto x = this->self().next_back();
        for (usize i = 0; i < n && x.is_some(); ++i)
            x = this->self().next_back();
        return x;
    }

    // Advances the iterator by up to `n`, returning the number actually advanced.
    auto advance_by(usize n) -> usize {
        usize i = 0;
        for (; i < n; ++i)
            if (this->self().next().is_none()) break;
        return i;
    }

    auto is_sorted() -> bool {
        auto prev = this->self().next();
        if (prev.is_none()) return true;
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            if (*x < *prev) return false;
            prev = rstd::move(x);
        }
        return true;
    }

    // key(item) -> comparable; checks the keys are non-decreasing.
    template<typename F>
    auto is_sorted_by_key(F key) -> bool {
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

    // Splits into two collections of type B by `pred`. B must be default-constructible
    // and expose `push`. Kept generic so core never names a concrete container.
    template<typename B, typename Pred>
    auto partition(Pred pred) -> rstd::tuple<B, B> {
        B yes {};
        B no {};
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            if (pred(*x))
                yes.push(rstd::move(*x));
            else
                no.push(rstd::move(*x));
        }
        return { rstd::move(yes), rstd::move(no) };
    }

    // Item must be a 2-tuple; splits into collections CA/CB (default-constructible, `push`).
    template<typename CA, typename CB>
    auto unzip() -> rstd::tuple<CA, CB> {
        CA a {};
        CB b {};
        for (auto x = this->self().next(); x.is_some(); x = this->self().next()) {
            a.push(rstd::move(x->template get<0>()));
            b.push(rstd::move(x->template get<1>()));
        }
        return { rstd::move(a), rstd::move(b) };
    }

    // Borrows the iterator so adapters can consume from it without moving it.
    auto by_ref() -> iter::ByRef<Self> { return iter::ByRef<Self>(rstd::addressof(this->self())); }

    // ---- adapters (move the receiver into the adapter) ----
    template<typename F>
    auto map(F f) -> iter::Map<Self, F> {
        return iter::Map<Self, F>(rstd::move(this->self()), rstd::move(f));
    }

    template<typename F>
    auto map_while(F f) -> iter::MapWhile<Self, F> {
        return iter::MapWhile<Self, F>(rstd::move(this->self()), rstd::move(f));
    }

    template<typename Pred>
    auto filter(Pred pred) -> iter::Filter<Self, Pred> {
        return iter::Filter<Self, Pred>(rstd::move(this->self()), rstd::move(pred));
    }

    template<typename F>
    auto filter_map(F f) -> iter::FilterMap<Self, F> {
        return iter::FilterMap<Self, F>(rstd::move(this->self()), rstd::move(f));
    }

    auto enumerate() -> iter::Enumerate<Self> {
        return iter::Enumerate<Self>(rstd::move(this->self()));
    }

    template<typename U>
    auto zip(U other) -> iter::Zip<Self, U> {
        return iter::Zip<Self, U>(rstd::move(this->self()), rstd::move(other));
    }

    auto take(usize n) -> iter::Take<Self> { return iter::Take<Self>(rstd::move(this->self()), n); }

    auto skip(usize n) -> iter::Skip<Self> { return iter::Skip<Self>(rstd::move(this->self()), n); }

    auto step_by(usize step) -> iter::StepBy<Self> {
        return iter::StepBy<Self>(rstd::move(this->self()), step);
    }

    template<typename Pred>
    auto take_while(Pred p) -> iter::TakeWhile<Self, Pred> {
        return iter::TakeWhile<Self, Pred>(rstd::move(this->self()), rstd::move(p));
    }

    template<typename Pred>
    auto skip_while(Pred p) -> iter::SkipWhile<Self, Pred> {
        return iter::SkipWhile<Self, Pred>(rstd::move(this->self()), rstd::move(p));
    }

    template<typename U>
    auto chain(U other) -> iter::Chain<Self, U> {
        return iter::Chain<Self, U>(rstd::move(this->self()), rstd::move(other));
    }

    auto rev() -> iter::Rev<Self> { return iter::Rev<Self>(rstd::move(this->self())); }

    auto peekable() -> iter::Peekable<Self> {
        return iter::Peekable<Self>(rstd::move(this->self()));
    }

    auto flatten() -> iter::Flatten<Self> {
        return iter::Flatten<Self>(rstd::move(this->self()));
    }

    template<typename F>
    auto flat_map(F f) -> iter::FlatMap<Self, F> {
        return iter::FlatMap<Self, F>(rstd::move(this->self()), rstd::move(f));
    }

    auto cloned() -> iter::Cloned<Self> { return iter::Cloned<Self>(rstd::move(this->self())); }

    auto copied() -> iter::Copied<Self> { return iter::Copied<Self>(rstd::move(this->self())); }

    template<typename F>
    auto inspect(F f) -> iter::Inspect<Self, F> {
        return iter::Inspect<Self, F>(rstd::move(this->self()), rstd::move(f));
    }

    template<typename St, typename F>
    auto scan(St st, F f) -> iter::Scan<Self, St, F> {
        return iter::Scan<Self, St, F>(rstd::move(this->self()), rstd::move(st), rstd::move(f));
    }

    auto fuse() -> iter::Fuse<Self> { return iter::Fuse<Self>(rstd::move(this->self())); }

    auto cycle() -> iter::Cycle<Self> { return iter::Cycle<Self>(rstd::move(this->self())); }

    template<class S = Self>
    auto intersperse(typename S::Item sep) -> iter::Intersperse<Self> {
        return iter::Intersperse<Self>(rstd::move(this->self()), rstd::move(sep));
    }
};

} // namespace rstd

export module rstd.core:iter.adapters;
export import :iter.traits;

namespace rstd::iter
{

template<class I, class F>
struct Map : DefaultInClass<Map<I, F>, Iterator> {
    using Item = mtp::invoke_result_t<F&, typename I::Item>;
    I i;
    F f;
    Map(I in, F fn): i(rstd::move(in)), f(rstd::move(fn)) {}
    auto next() -> Option<Item> {
        auto x = as<Iterator>(i).next();
        if (x.is_none()) return rstd::None();
        return rstd::Some(f(rstd::move(*x)));
    }
    auto size_hint() const -> SizeHint { return as<Iterator>(i).size_hint(); }
    auto next_back() -> Option<Item>
        requires Impled<I, DoubleEndedIterator>
    {
        auto x = as<DoubleEndedIterator>(i).next_back();
        if (x.is_none()) return rstd::None();
        return rstd::Some(f(rstd::move(*x)));
    }
    auto len() const -> usize
        requires Impled<I, ExactSizeIterator>
    {
        return as<ExactSizeIterator>(i).len();
    }
};

template<class I, class F>
struct MapWhile : DefaultInClass<MapWhile<I, F>, Iterator> {
    using Item = typename mtp::invoke_result_t<F&, typename I::Item>::value_type;
    I i;
    F f;
    MapWhile(I in, F fn): i(rstd::move(in)), f(rstd::move(fn)) {}
    auto next() -> Option<Item> {
        auto x = i.next();
        if (x.is_none()) return rstd::None();
        return f(rstd::move(*x));
    }
};

template<class I, class P>
struct Filter : DefaultInClass<Filter<I, P>, Iterator> {
    using Item = typename I::Item;
    I i;
    P pred;
    Filter(I in, P p): i(rstd::move(in)), pred(rstd::move(p)) {}
    auto next() -> Option<Item> {
        for (auto x = i.next(); x.is_some(); x = i.next())
            if (pred(*x)) return x;
        return rstd::None();
    }
};

template<class I, class F>
struct FilterMap : DefaultInClass<FilterMap<I, F>, Iterator> {
    using Item = typename mtp::invoke_result_t<F&, typename I::Item>::value_type;
    I i;
    F f;
    FilterMap(I in, F fn): i(rstd::move(in)), f(rstd::move(fn)) {}
    auto next() -> Option<Item> {
        for (auto x = i.next(); x.is_some(); x = i.next()) {
            auto r = f(rstd::move(*x));
            if (r.is_some()) return r;
        }
        return rstd::None();
    }
};

template<class I>
struct Enumerate : DefaultInClass<Enumerate<I>, Iterator> {
    using Item = rstd::tuple<usize, typename I::Item>;
    I     i;
    usize count;
    explicit Enumerate(I in): i(rstd::move(in)), count(0) {}
    auto next() -> Option<Item> {
        auto x = i.next();
        if (x.is_none()) return rstd::None();
        usize idx = count++;
        return rstd::Some(Item(idx, rstd::move(*x)));
    }
    auto size_hint() const -> SizeHint { return i.size_hint(); }
};

template<class A, class B>
struct Zip : DefaultInClass<Zip<A, B>, Iterator> {
    using Item = rstd::tuple<typename A::Item, typename B::Item>;
    A a;
    B b;
    Zip(A ain, B bin): a(rstd::move(ain)), b(rstd::move(bin)) {}
    auto next() -> Option<Item> {
        auto x = a.next();
        if (x.is_none()) return rstd::None();
        auto y = b.next();
        if (y.is_none()) return rstd::None();
        return rstd::Some(Item(rstd::move(*x), rstd::move(*y)));
    }
};

template<class I>
struct Take : DefaultInClass<Take<I>, Iterator> {
    using Item = typename I::Item;
    I     i;
    usize n;
    Take(I in, usize cnt): i(rstd::move(in)), n(cnt) {}
    auto next() -> Option<Item> {
        if (n == 0) return rstd::None();
        --n;
        return i.next();
    }
};

template<class I>
struct Skip : DefaultInClass<Skip<I>, Iterator> {
    using Item = typename I::Item;
    I     i;
    usize n;
    Skip(I in, usize cnt): i(rstd::move(in)), n(cnt) {}
    auto next() -> Option<Item> {
        while (n > 0) {
            auto x = i.next();
            --n;
            if (x.is_none()) return rstd::None();
        }
        return i.next();
    }
};

template<class I, class P>
struct TakeWhile : DefaultInClass<TakeWhile<I, P>, Iterator> {
    using Item = typename I::Item;
    I    i;
    P    pred;
    bool done;
    TakeWhile(I in, P p): i(rstd::move(in)), pred(rstd::move(p)), done(false) {}
    auto next() -> Option<Item> {
        if (done) return rstd::None();
        auto x = i.next();
        if (x.is_some() && pred(*x)) return x;
        done = true;
        return rstd::None();
    }
};

template<class I, class P>
struct SkipWhile : DefaultInClass<SkipWhile<I, P>, Iterator> {
    using Item = typename I::Item;
    I    i;
    P    pred;
    bool skipping;
    SkipWhile(I in, P p): i(rstd::move(in)), pred(rstd::move(p)), skipping(true) {}
    auto next() -> Option<Item> {
        if (skipping) {
            for (auto x = i.next(); x.is_some(); x = i.next()) {
                if (! pred(*x)) {
                    skipping = false;
                    return x;
                }
            }
            skipping = false;
            return rstd::None();
        }
        return i.next();
    }
};

template<class I>
struct StepBy : DefaultInClass<StepBy<I>, Iterator> {
    using Item = typename I::Item;
    I     i;
    usize step;
    bool  first;
    StepBy(I in, usize s): i(rstd::move(in)), step(s), first(true) {}
    auto next() -> Option<Item> {
        if (first) {
            first = false;
            return i.next();
        }
        for (usize k = 0; k + 1 < step; ++k)
            if (i.next().is_none()) return rstd::None();
        return i.next();
    }
};

template<class A, class B>
struct Chain : DefaultInClass<Chain<A, B>, Iterator> {
    using Item = typename A::Item;
    A    a;
    B    b;
    bool a_done;
    Chain(A ain, B bin): a(rstd::move(ain)), b(rstd::move(bin)), a_done(false) {}
    auto next() -> Option<Item> {
        if (! a_done) {
            auto x = a.next();
            if (x.is_some()) return x;
            a_done = true;
        }
        return b.next();
    }
};

template<class I>
struct Cloned : DefaultInClass<Cloned<I>, Iterator> {
    using Item = mtp::rm_cvf<decltype(*mtp::declval<typename I::Item>())>;
    I i;
    explicit Cloned(I in): i(rstd::move(in)) {}
    auto next() -> Option<Item> {
        auto x = i.next();
        if (x.is_none()) return rstd::None();
        Item v = as<clone::Clone>(**x).clone();
        return rstd::Some(rstd::move(v));
    }
    auto size_hint() const -> SizeHint { return i.size_hint(); }
};

template<class I>
struct Copied : DefaultInClass<Copied<I>, Iterator> {
    using Item = mtp::rm_cvf<decltype(*mtp::declval<typename I::Item>())>;
    I i;
    explicit Copied(I in): i(rstd::move(in)) {}
    auto next() -> Option<Item> {
        auto x = i.next();
        if (x.is_none()) return rstd::None();
        Item v = **x;
        return rstd::Some(rstd::move(v));
    }
    auto size_hint() const -> SizeHint { return i.size_hint(); }
};

template<class I, class F>
struct Inspect : DefaultInClass<Inspect<I, F>, Iterator> {
    using Item = typename I::Item;
    I i;
    F f;
    Inspect(I in, F fn): i(rstd::move(in)), f(rstd::move(fn)) {}
    auto next() -> Option<Item> {
        auto x = i.next();
        if (x.is_some()) f(*x);
        return x;
    }
    auto size_hint() const -> SizeHint { return i.size_hint(); }
};

template<class I, class St, class F>
struct Scan : DefaultInClass<Scan<I, St, F>, Iterator> {
    using Item = typename mtp::invoke_result_t<F&, St&, typename I::Item>::value_type;
    I  i;
    St st;
    F  f;
    Scan(I in, St s, F fn): i(rstd::move(in)), st(rstd::move(s)), f(rstd::move(fn)) {}
    auto next() -> Option<Item> {
        auto x = i.next();
        if (x.is_none()) return rstd::None();
        return f(st, rstd::move(*x));
    }
};

template<class I>
struct Fuse : DefaultInClass<Fuse<I>, Iterator> {
    using Item = typename I::Item;
    I    i;
    bool done;
    explicit Fuse(I in): i(rstd::move(in)), done(false) {}
    auto next() -> Option<Item> {
        if (done) return rstd::None();
        auto x = i.next();
        if (x.is_none()) done = true;
        return x;
    }
};

template<class I>
struct Peekable : DefaultInClass<Peekable<I>, Iterator> {
    using Item = typename I::Item;
    I            i;
    Option<Item> peeked;
    explicit Peekable(I in): i(rstd::move(in)), peeked(rstd::None()) {}
    auto next() -> Option<Item> {
        if (peeked.is_some()) return peeked.take();
        return i.next();
    }
    auto peek() -> const Item* {
        if (peeked.is_none()) peeked = i.next();
        if (peeked.is_none()) return nullptr;
        return &*peeked;
    }
    auto size_hint() const -> SizeHint { return i.size_hint(); }
};

template<class I>
struct Rev : DefaultInClass<Rev<I>, Iterator> {
    using Item = typename I::Item;
    I i;
    explicit Rev(I in): i(rstd::move(in)) {}
    auto next() -> Option<Item> { return i.next_back(); }
    auto next_back() -> Option<Item> { return i.next(); }
    auto size_hint() const -> SizeHint { return i.size_hint(); }
};

template<class I>
struct Cycle : DefaultInClass<Cycle<I>, Iterator> {
    using Item = typename I::Item;
    I orig;
    I cur;
    explicit Cycle(I in): orig(as<clone::Clone>(in).clone()), cur(rstd::move(in)) {}
    auto next() -> Option<Item> {
        auto x = cur.next();
        if (x.is_some()) return x;
        cur = as<clone::Clone>(orig).clone();
        return cur.next();
    }
};

// Turns an iterator's item into an iterator: uses `into_iter()` when available
// (collections), otherwise treats the item as already being an iterator.
template<class X>
constexpr auto into_iter_of(X&& x) {
    if constexpr (requires { rstd::forward<X>(x).into_iter(); })
        return rstd::forward<X>(x).into_iter();
    else
        return rstd::forward<X>(x);
}

template<class I>
struct Flatten : DefaultInClass<Flatten<I>, Iterator> {
    using Inner = decltype(into_iter_of(mtp::declval<typename I::Item>()));
    using Item  = typename Inner::Item;
    I             i;
    Option<Inner> cur;
    explicit Flatten(I in): i(rstd::move(in)), cur(rstd::None()) {}
    auto next() -> Option<Item> {
        for (;;) {
            if (cur.is_some()) {
                auto x = cur->next();
                if (x.is_some()) return x;
                cur = rstd::None();
            }
            auto outer = i.next();
            if (outer.is_none()) return rstd::None();
            cur = rstd::Some(into_iter_of(rstd::move(*outer)));
        }
    }
};

template<class I, class F>
struct FlatMap : DefaultInClass<FlatMap<I, F>, Iterator> {
    using Mapped = mtp::invoke_result_t<F&, typename I::Item>;
    using Inner  = decltype(into_iter_of(mtp::declval<Mapped>()));
    using Item   = typename Inner::Item;
    I             i;
    F             f;
    Option<Inner> cur;
    FlatMap(I in, F fn): i(rstd::move(in)), f(rstd::move(fn)), cur(rstd::None()) {}
    auto next() -> Option<Item> {
        for (;;) {
            if (cur.is_some()) {
                auto x = cur->next();
                if (x.is_some()) return x;
                cur = rstd::None();
            }
            auto outer = i.next();
            if (outer.is_none()) return rstd::None();
            cur = rstd::Some(into_iter_of(f(rstd::move(*outer))));
        }
    }
};

// Borrows an iterator by pointer so it can be partially consumed without moving.
template<class I>
struct ByRef : DefaultInClass<ByRef<I>, Iterator> {
    using Item = typename I::Item;
    I* inner;
    explicit ByRef(I* p): inner(p) {}
    auto next() -> Option<Item> { return inner->next(); }
    auto size_hint() const -> SizeHint { return inner->size_hint(); }
};

template<class I>
struct Intersperse : DefaultInClass<Intersperse<I>, Iterator> {
    using Item = typename I::Item;
    I            i;
    Item         sep;
    Option<Item> peeked;
    bool         emit_sep;
    Intersperse(I in, Item s)
        : i(rstd::move(in)), sep(rstd::move(s)), peeked(rstd::None()), emit_sep(false) {
        peeked = i.next();
    }
    auto next() -> Option<Item> {
        if (emit_sep) {
            if (peeked.is_some()) {
                emit_sep = false;
                return rstd::Some(as<clone::Clone>(sep).clone());
            }
            return rstd::None();
        }
        if (peeked.is_none()) return rstd::None();
        auto out = peeked.take();
        peeked   = i.next();
        if (peeked.is_some()) emit_sep = true;
        return out;
    }
};

} // namespace rstd::iter

export module rstd.core:iter.adapters;
import :num.types;
export import :iter.traits;

namespace rstd::iter
{

template<class I, class F>
struct Map : DefaultInClass<Map<I, F>, Iterator> {
    using Item                                = mtp::invoke_result_t<F&, typename I::Item>;
    static constexpr bool PROVEN_DOUBLE_ENDED = Impled<I, DoubleEndedIterator>;
    static constexpr bool PROVEN_EXACT_SIZE   = Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED        = Impled<I, FusedIterator>;
    static constexpr bool PROVEN_TRUSTED_LEN  = Impled<I, TrustedLen>;
    I                     i;
    F                     f;
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
    auto size_hint() const -> SizeHint { return { usize(), i.size_hint().template get<1>() }; }
};

template<class I, class P>
struct Filter : DefaultInClass<Filter<I, P>, Iterator> {
    using Item                                = typename I::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED = Impled<I, DoubleEndedIterator>;
    static constexpr bool PROVEN_FUSED        = Impled<I, FusedIterator>;
    I                     i;
    P                     pred;
    Filter(I in, P p): i(rstd::move(in)), pred(rstd::move(p)) {}
    auto next() -> Option<Item> {
        for (auto x = i.next(); x.is_some(); x = i.next())
            if (pred(*x)) return x;
        return rstd::None();
    }
    auto next_back() -> Option<Item>
        requires Impled<I, DoubleEndedIterator>
    {
        auto backwards = as<DoubleEndedIterator>(i);
        for (auto x = backwards.next_back(); x.is_some(); x = backwards.next_back())
            if (pred(*x)) return x;
        return rstd::None();
    }
    auto size_hint() const -> SizeHint { return { usize(), i.size_hint().template get<1>() }; }
};

template<class I, class F>
struct FilterMap : DefaultInClass<FilterMap<I, F>, Iterator> {
    using Item = typename mtp::invoke_result_t<F&, typename I::Item>::value_type;
    static constexpr bool PROVEN_DOUBLE_ENDED = Impled<I, DoubleEndedIterator>;
    static constexpr bool PROVEN_FUSED        = Impled<I, FusedIterator>;
    I                     i;
    F                     f;
    FilterMap(I in, F fn): i(rstd::move(in)), f(rstd::move(fn)) {}
    auto next() -> Option<Item> {
        for (auto x = i.next(); x.is_some(); x = i.next()) {
            auto r = f(rstd::move(*x));
            if (r.is_some()) return r;
        }
        return rstd::None();
    }
    auto next_back() -> Option<Item>
        requires Impled<I, DoubleEndedIterator>
    {
        auto backwards = as<DoubleEndedIterator>(i);
        for (auto x = backwards.next_back(); x.is_some(); x = backwards.next_back()) {
            auto result = f(rstd::move(*x));
            if (result.is_some()) return result;
        }
        return rstd::None();
    }
    auto size_hint() const -> SizeHint { return { usize(), i.size_hint().template get<1>() }; }
};

template<class I>
struct Enumerate : DefaultInClass<Enumerate<I>, Iterator> {
    using Item = rstd::tuple<usize, typename I::Item>;
    static constexpr bool PROVEN_DOUBLE_ENDED =
        Impled<I, DoubleEndedIterator> && Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_EXACT_SIZE  = Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED       = Impled<I, FusedIterator>;
    static constexpr bool PROVEN_TRUSTED_LEN = Impled<I, TrustedLen>;
    I                     i;
    usize                 count;
    explicit Enumerate(I in): i(rstd::move(in)), count() {}
    auto next() -> Option<Item> {
        auto x = i.next();
        if (x.is_none()) return rstd::None();
        usize idx = count;
        ++count;
        return rstd::Some(Item(idx, rstd::move(*x)));
    }
    auto size_hint() const -> SizeHint { return i.size_hint(); }
    auto next_back() -> Option<Item>
        requires Impled<I, DoubleEndedIterator> && Impled<I, ExactSizeIterator>
    {
        auto remaining = as<ExactSizeIterator>(i).len();
        auto x         = as<DoubleEndedIterator>(i).next_back();
        if (x.is_none()) return rstd::None();
        return rstd::Some(Item(count + remaining - usize(1), rstd::move(*x)));
    }
    auto len() const -> usize
        requires Impled<I, ExactSizeIterator>
    {
        return as<ExactSizeIterator>(i).len();
    }
};

template<class A, class B>
struct Zip : DefaultInClass<Zip<A, B>, Iterator> {
    using Item = rstd::tuple<typename A::Item, typename B::Item>;
    static constexpr bool PROVEN_DOUBLE_ENDED =
        Impled<A, DoubleEndedIterator> && Impled<A, ExactSizeIterator> &&
        Impled<B, DoubleEndedIterator> && Impled<B, ExactSizeIterator>;
    static constexpr bool PROVEN_EXACT_SIZE =
        Impled<A, ExactSizeIterator> && Impled<B, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED       = Impled<A, FusedIterator> && Impled<B, FusedIterator>;
    static constexpr bool PROVEN_TRUSTED_LEN = Impled<A, TrustedLen> && Impled<B, TrustedLen>;
    A                     a;
    B                     b;
    Zip(A ain, B bin): a(rstd::move(ain)), b(rstd::move(bin)) {}
    auto next() -> Option<Item> {
        auto x = a.next();
        if (x.is_none()) return rstd::None();
        auto y = b.next();
        if (y.is_none()) return rstd::None();
        return rstd::Some(Item(rstd::move(*x), rstd::move(*y)));
    }
    auto next_back() -> Option<Item>
        requires PROVEN_DOUBLE_ENDED
    {
        auto a_back = as<DoubleEndedIterator>(a);
        auto b_back = as<DoubleEndedIterator>(b);
        auto a_len  = as<ExactSizeIterator>(a).len();
        auto b_len  = as<ExactSizeIterator>(b).len();
        while (a_len > b_len) {
            (void)a_back.next_back();
            --a_len;
        }
        while (b_len > a_len) {
            (void)b_back.next_back();
            --b_len;
        }
        auto x = a_back.next_back();
        if (x.is_none()) return rstd::None();
        auto y = b_back.next_back();
        if (y.is_none()) return rstd::None();
        return rstd::Some(Item(rstd::move(*x), rstd::move(*y)));
    }
    auto size_hint() const -> SizeHint {
        auto a_hint = a.size_hint();
        auto b_hint = b.size_hint();
        auto lower = a_hint.template get<0>() < b_hint.template get<0>() ? a_hint.template get<0>()
                                                                         : b_hint.template get<0>();
        auto a_upper = a_hint.template get<1>();
        auto b_upper = b_hint.template get<1>();
        if (a_upper.is_none()) return { lower, b_upper };
        if (b_upper.is_none()) return { lower, a_upper };
        return { lower, rstd::Some(*a_upper < *b_upper ? *a_upper : *b_upper) };
    }
    auto len() const -> usize
        requires PROVEN_EXACT_SIZE
    {
        auto a_len = as<ExactSizeIterator>(a).len();
        auto b_len = as<ExactSizeIterator>(b).len();
        return a_len < b_len ? a_len : b_len;
    }
};

template<class I>
struct Take : DefaultInClass<Take<I>, Iterator> {
    using Item = typename I::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED =
        Impled<I, DoubleEndedIterator> && Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_EXACT_SIZE  = Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED       = Impled<I, FusedIterator>;
    static constexpr bool PROVEN_TRUSTED_LEN = Impled<I, TrustedLen>;
    I                     i;
    usize                 n;
    Take(I in, usize cnt): i(rstd::move(in)), n(cnt) {}
    auto next() -> Option<Item> {
        if (n == usize()) return rstd::None();
        --n;
        return i.next();
    }
    auto next_back() -> Option<Item>
        requires PROVEN_DOUBLE_ENDED
    {
        auto backwards = as<DoubleEndedIterator>(i);
        auto length    = as<ExactSizeIterator>(i).len();
        while (length > n) {
            (void)backwards.next_back();
            --length;
        }
        if (n == usize()) return rstd::None();
        --n;
        return backwards.next_back();
    }
    auto size_hint() const -> SizeHint {
        auto hint  = i.size_hint();
        auto lower = hint.template get<0>() < n ? hint.template get<0>() : n;
        auto upper = hint.template get<1>();
        if (upper.is_none() || *upper > n) upper = rstd::Some(usize(n.to_primitive()));
        return { lower, rstd::move(upper) };
    }
    auto len() const -> usize
        requires PROVEN_EXACT_SIZE
    {
        auto length = as<ExactSizeIterator>(i).len();
        return length < n ? length : n;
    }
};

template<class I>
struct Skip : DefaultInClass<Skip<I>, Iterator> {
    using Item = typename I::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED =
        Impled<I, DoubleEndedIterator> && Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_EXACT_SIZE = Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED      = Impled<I, FusedIterator>;
    I                     i;
    usize                 n;
    Skip(I in, usize cnt): i(rstd::move(in)), n(cnt) {}
    auto next() -> Option<Item> {
        while (n != usize()) {
            auto x = i.next();
            --n;
            if (x.is_none()) return rstd::None();
        }
        return i.next();
    }
    auto next_back() -> Option<Item>
        requires PROVEN_DOUBLE_ENDED
    {
        if (as<ExactSizeIterator>(i).len() <= n) return rstd::None();
        return as<DoubleEndedIterator>(i).next_back();
    }
    auto size_hint() const -> SizeHint {
        auto hint  = i.size_hint();
        auto lower = hint.template get<0>() > n ? hint.template get<0>() - n : usize();
        auto upper = hint.template get<1>();
        if (upper.is_some()) *upper = *upper > n ? *upper - n : usize();
        return { lower, rstd::move(upper) };
    }
    auto len() const -> usize
        requires PROVEN_EXACT_SIZE
    {
        auto length = as<ExactSizeIterator>(i).len();
        return length > n ? length - n : usize();
    }
};

template<class I, class P>
struct TakeWhile : DefaultInClass<TakeWhile<I, P>, Iterator> {
    using Item                         = typename I::Item;
    static constexpr bool PROVEN_FUSED = Impled<I, FusedIterator>;
    I                     i;
    P                     pred;
    bool                  done;
    TakeWhile(I in, P p): i(rstd::move(in)), pred(rstd::move(p)), done(false) {}
    auto next() -> Option<Item> {
        if (done) return rstd::None();
        auto x = i.next();
        if (x.is_none()) return rstd::None();
        if (pred(*x)) return x;
        done = true;
        return rstd::None();
    }
    auto size_hint() const -> SizeHint {
        if (done) return { usize(), rstd::Some(usize()) };
        return { usize(), i.size_hint().template get<1>() };
    }
};

template<class I, class P>
struct SkipWhile : DefaultInClass<SkipWhile<I, P>, Iterator> {
    using Item                         = typename I::Item;
    static constexpr bool PROVEN_FUSED = Impled<I, FusedIterator>;
    I                     i;
    P                     pred;
    bool                  skipping;
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
    auto size_hint() const -> SizeHint {
        if (! skipping) return i.size_hint();
        return { usize(), i.size_hint().template get<1>() };
    }
};

template<class I>
struct StepBy : DefaultInClass<StepBy<I>, Iterator> {
    using Item = typename I::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED =
        Impled<I, DoubleEndedIterator> && Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_EXACT_SIZE = Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED      = Impled<I, FusedIterator>;
    I                     i;
    usize                 step;
    bool                  first;
    StepBy(I in, usize s): i(rstd::move(in)), step(s), first(true) {
        if (step == usize()) rstd::panic("step_by called with step 0");
    }
    auto next() -> Option<Item> {
        if (first) {
            first = false;
            return i.next();
        }
        for (rstd::size_t k = 0; k + rstd::size_t(1) < step.to_primitive(); ++k)
            if (i.next().is_none()) return rstd::None();
        return i.next();
    }
    auto next_back() -> Option<Item>
        requires PROVEN_DOUBLE_ENDED
    {
        auto backwards = as<DoubleEndedIterator>(i);
        auto length    = as<ExactSizeIterator>(i).len().to_primitive();
        if (length == 0) return rstd::None();
        auto stride  = step.to_primitive();
        auto discard = first ? (length - 1) % stride
                             : (length >= stride ? (length - stride) % stride : length);
        for (rstd::size_t count = 0; count < discard; ++count) (void)backwards.next_back();
        if (! first && length < stride) return rstd::None();
        return backwards.next_back();
    }
    auto size_hint() const -> SizeHint {
        auto hint  = i.size_hint();
        auto count = [this](usize length) {
            auto raw    = length.to_primitive();
            auto stride = step.to_primitive();
            if (first) return usize(raw == 0 ? 0 : 1 + (raw - 1) / stride);
            return usize(raw / stride);
        };
        auto upper = hint.template get<1>();
        if (upper.is_some()) *upper = count(*upper);
        return { count(hint.template get<0>()), rstd::move(upper) };
    }
    auto len() const -> usize
        requires PROVEN_EXACT_SIZE
    {
        auto raw    = as<ExactSizeIterator>(i).len().to_primitive();
        auto stride = step.to_primitive();
        return first ? usize(raw == 0 ? 0 : 1 + (raw - 1) / stride) : usize(raw / stride);
    }
};

template<class A, class B>
struct Chain : DefaultInClass<Chain<A, B>, Iterator> {
    using Item = typename A::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED =
        Impled<A, DoubleEndedIterator> && Impled<B, DoubleEndedIterator>;
    static constexpr bool PROVEN_FUSED       = Impled<A, FusedIterator> && Impled<B, FusedIterator>;
    static constexpr bool PROVEN_TRUSTED_LEN = Impled<A, TrustedLen> && Impled<B, TrustedLen>;
    A                     a;
    B                     b;
    bool                  a_done;
    bool                  b_done;
    Chain(A ain, B bin): a(rstd::move(ain)), b(rstd::move(bin)), a_done(false), b_done(false) {}
    auto next() -> Option<Item> {
        if (! a_done) {
            auto x = a.next();
            if (x.is_some()) return x;
            a_done = true;
        }
        if (b_done) return rstd::None();
        return b.next();
    }
    auto next_back() -> Option<Item>
        requires PROVEN_DOUBLE_ENDED
    {
        if (! b_done) {
            auto x = as<DoubleEndedIterator>(b).next_back();
            if (x.is_some()) return x;
            b_done = true;
        }
        if (a_done) return rstd::None();
        return as<DoubleEndedIterator>(a).next_back();
    }
    auto size_hint() const -> SizeHint {
        if (a_done && b_done) return { usize(), rstd::Some(usize()) };
        if (a_done) return b.size_hint();
        if (b_done) return a.size_hint();
        auto a_hint  = a.size_hint();
        auto b_hint  = b.size_hint();
        auto a_lower = a_hint.template get<0>();
        auto b_lower = b_hint.template get<0>();
        auto lower   = a_lower > usize::MAX - b_lower ? usize::MAX : a_lower + b_lower;
        auto a_upper = a_hint.template get<1>();
        auto b_upper = b_hint.template get<1>();
        if (a_upper.is_none() || b_upper.is_none() || *a_upper > usize::MAX - *b_upper)
            return { lower, rstd::None() };
        return { lower, rstd::Some(*a_upper + *b_upper) };
    }
};

template<class I>
struct Cloned : DefaultInClass<Cloned<I>, Iterator> {
    using Item                                = details::observed_item_t<typename I::Item>;
    static constexpr bool PROVEN_DOUBLE_ENDED = Impled<I, DoubleEndedIterator>;
    static constexpr bool PROVEN_EXACT_SIZE   = Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED        = Impled<I, FusedIterator>;
    static constexpr bool PROVEN_TRUSTED_LEN  = Impled<I, TrustedLen>;
    I                     i;
    explicit Cloned(I in): i(rstd::move(in)) {}
    auto next() -> Option<Item> {
        auto x = i.next();
        if (x.is_none()) return rstd::None();
        Item v = as<clone::Clone>(details::observe_item(*x)).clone();
        return rstd::Some(rstd::move(v));
    }
    auto size_hint() const -> SizeHint { return i.size_hint(); }
    auto next_back() -> Option<Item>
        requires Impled<I, DoubleEndedIterator>
    {
        auto x = as<DoubleEndedIterator>(i).next_back();
        if (x.is_none()) return rstd::None();
        return rstd::Some(as<clone::Clone>(details::observe_item(*x)).clone());
    }
    auto len() const -> usize
        requires Impled<I, ExactSizeIterator>
    {
        return as<ExactSizeIterator>(i).len();
    }
};

template<class I>
struct Copied : DefaultInClass<Copied<I>, Iterator> {
    using Item                                = details::observed_item_t<typename I::Item>;
    static constexpr bool PROVEN_DOUBLE_ENDED = Impled<I, DoubleEndedIterator>;
    static constexpr bool PROVEN_EXACT_SIZE   = Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED        = Impled<I, FusedIterator>;
    static constexpr bool PROVEN_TRUSTED_LEN  = Impled<I, TrustedLen>;
    I                     i;
    explicit Copied(I in): i(rstd::move(in)) {}
    auto next() -> Option<Item> {
        auto x = i.next();
        if (x.is_none()) return rstd::None();
        Item v = details::observe_item(*x);
        return rstd::Some(rstd::move(v));
    }
    auto size_hint() const -> SizeHint { return i.size_hint(); }
    auto next_back() -> Option<Item>
        requires Impled<I, DoubleEndedIterator>
    {
        auto x = as<DoubleEndedIterator>(i).next_back();
        if (x.is_none()) return rstd::None();
        return rstd::Some(Item(details::observe_item(*x)));
    }
    auto len() const -> usize
        requires Impled<I, ExactSizeIterator>
    {
        return as<ExactSizeIterator>(i).len();
    }
};

template<class I, class F>
struct Inspect : DefaultInClass<Inspect<I, F>, Iterator> {
    using Item                                = typename I::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED = Impled<I, DoubleEndedIterator>;
    static constexpr bool PROVEN_EXACT_SIZE   = Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED        = Impled<I, FusedIterator>;
    static constexpr bool PROVEN_TRUSTED_LEN  = Impled<I, TrustedLen>;
    I                     i;
    F                     f;
    Inspect(I in, F fn): i(rstd::move(in)), f(rstd::move(fn)) {}
    auto next() -> Option<Item> {
        auto x = i.next();
        if (x.is_some()) f(*x);
        return x;
    }
    auto size_hint() const -> SizeHint { return i.size_hint(); }
    auto next_back() -> Option<Item>
        requires Impled<I, DoubleEndedIterator>
    {
        auto x = as<DoubleEndedIterator>(i).next_back();
        if (x.is_some()) f(*x);
        return x;
    }
    auto len() const -> usize
        requires Impled<I, ExactSizeIterator>
    {
        return as<ExactSizeIterator>(i).len();
    }
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
    auto size_hint() const -> SizeHint { return { usize(), i.size_hint().template get<1>() }; }
};

template<class I>
struct Fuse : DefaultInClass<Fuse<I>, Iterator> {
    using Item                                = typename I::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED = Impled<I, DoubleEndedIterator>;
    static constexpr bool PROVEN_EXACT_SIZE   = Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = Impled<I, TrustedLen>;
    I                     i;
    bool                  done;
    explicit Fuse(I in): i(rstd::move(in)), done(false) {}
    auto next() -> Option<Item> {
        if (done) return rstd::None();
        auto x = i.next();
        if (x.is_none()) done = true;
        return x;
    }
    auto next_back() -> Option<Item>
        requires Impled<I, DoubleEndedIterator>
    {
        if (done) return rstd::None();
        auto x = as<DoubleEndedIterator>(i).next_back();
        if (x.is_none()) done = true;
        return x;
    }
    auto size_hint() const -> SizeHint {
        return done ? SizeHint(usize(), rstd::Some(usize())) : i.size_hint();
    }
    auto len() const -> usize
        requires Impled<I, ExactSizeIterator>
    {
        return done ? usize() : as<ExactSizeIterator>(i).len();
    }
};

template<class I>
struct Peekable : DefaultInClass<Peekable<I>, Iterator> {
    using Item                                = typename I::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED = Impled<I, DoubleEndedIterator>;
    static constexpr bool PROVEN_EXACT_SIZE   = Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED        = Impled<I, FusedIterator>;
    static constexpr bool PROVEN_TRUSTED_LEN  = Impled<I, TrustedLen>;
    I                     i;
    Option<Option<Item>>  peeked;
    explicit Peekable(I in): i(rstd::move(in)), peeked() {}
    auto next() -> Option<Item> {
        if (peeked.is_some()) return peeked.take().unwrap_unchecked();
        return i.next();
    }
    auto peek() -> const mtp::rm_ref<Item>* {
        if (peeked.is_none()) peeked = rstd::Some(i.next());
        if (peeked->is_none()) return nullptr;
        return rstd::addressof(**peeked);
    }
    auto size_hint() const -> SizeHint {
        if (peeked.is_some() && peeked->is_none()) return { usize(), rstd::Some(usize()) };
        auto hint = i.size_hint();
        if (peeked.is_none()) return hint;
        auto lower =
            hint.template get<0>() == usize::MAX ? usize::MAX : hint.template get<0>() + usize(1);
        auto upper = hint.template get<1>();
        if (upper.is_some()) {
            if (*upper == usize::MAX)
                upper = rstd::None();
            else
                ++*upper;
        }
        return { lower, rstd::move(upper) };
    }
    auto next_back() -> Option<Item>
        requires Impled<I, DoubleEndedIterator>
    {
        if (peeked.is_some() && peeked->is_none()) return rstd::None();
        auto x = as<DoubleEndedIterator>(i).next_back();
        if (x.is_some() || peeked.is_none()) return x;
        return peeked.take().unwrap_unchecked();
    }
    auto len() const -> usize
        requires Impled<I, ExactSizeIterator>
    {
        if (peeked.is_some() && peeked->is_none()) return usize();
        auto length = as<ExactSizeIterator>(i).len();
        return peeked.is_some() ? length + usize(1) : length;
    }
};

template<class I>
struct Rev : DefaultInClass<Rev<I>, Iterator> {
    using Item                                = typename I::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED = Impled<I, DoubleEndedIterator>;
    static constexpr bool PROVEN_EXACT_SIZE =
        Impled<I, DoubleEndedIterator> && Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED = Impled<I, DoubleEndedIterator> && Impled<I, FusedIterator>;
    static constexpr bool PROVEN_TRUSTED_LEN =
        Impled<I, DoubleEndedIterator> && Impled<I, TrustedLen>;
    I i;
    explicit Rev(I in): i(rstd::move(in)) {}
    auto next() -> Option<Item> { return as<DoubleEndedIterator>(i).next_back(); }
    auto next_back() -> Option<Item> { return i.next(); }
    auto size_hint() const -> SizeHint { return i.size_hint(); }
    auto len() const -> usize
        requires Impled<I, ExactSizeIterator>
    {
        return as<ExactSizeIterator>(i).len();
    }
};

template<class I>
struct Cycle : DefaultInClass<Cycle<I>, Iterator> {
    using Item                         = typename I::Item;
    static constexpr bool PROVEN_FUSED = true;
    I                     orig;
    I                     cur;
    explicit Cycle(I in): orig(as<clone::Clone>(in).clone()), cur(rstd::move(in)) {}
    auto next() -> Option<Item> {
        auto x = cur.next();
        if (x.is_some()) return x;
        cur = as<clone::Clone>(orig).clone();
        return cur.next();
    }
    auto size_hint() const -> SizeHint {
        auto hint = orig.size_hint();
        if (hint.template get<0>() == usize() && hint.template get<1>() == rstd::Some(usize()))
            return hint;
        if (hint.template get<0>() == usize()) return { usize(), rstd::None() };
        return { usize::MAX, rstd::None() };
    }
};

template<class I>
struct Flatten : DefaultInClass<Flatten<I>, Iterator> {
    using Outer = typename I::Item;
    using Inner = typename Impl<IntoIterator, mtp::rm_cvf<Outer>>::IntoIter;
    using Item  = typename Inner::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED =
        Impled<I, DoubleEndedIterator> && Impled<Inner, DoubleEndedIterator>;
    static constexpr bool PROVEN_FUSED = Impled<I, FusedIterator>;

    Fuse<I>       i;
    Option<Inner> front;
    Option<Inner> back;

    explicit Flatten(I in): i(Fuse<I>(rstd::move(in))), front(), back() {}

    auto next() -> Option<Item> {
        for (;;) {
            if (front.is_some()) {
                auto x = front->next();
                if (x.is_some()) return x;
                front = rstd::None();
            }
            auto outer = i.next();
            if (outer.is_none()) break;
            front = rstd::Some(iter::into_iter(rstd::move(*outer)));
        }

        if (back.is_none()) return rstd::None();
        auto item = back->next();
        if (item.is_none()) back = rstd::None();
        return item;
    }

    auto next_back() -> Option<Item>
        requires Impled<I, DoubleEndedIterator> && Impled<Inner, DoubleEndedIterator>
    {
        for (;;) {
            if (back.is_some()) {
                auto x = as<DoubleEndedIterator>(*back).next_back();
                if (x.is_some()) return x;
                back = rstd::None();
            }
            auto outer = as<DoubleEndedIterator>(i).next_back();
            if (outer.is_none()) break;
            back = rstd::Some(iter::into_iter(rstd::move(*outer)));
        }

        if (front.is_none()) return rstd::None();
        auto item = as<DoubleEndedIterator>(*front).next_back();
        if (item.is_none()) front = rstd::None();
        return item;
    }

    auto size_hint() const -> SizeHint {
        auto lower = usize();
        if (front.is_some()) lower += front->size_hint().template get<0>();
        if (back.is_some()) lower += back->size_hint().template get<0>();
        return { lower, rstd::None() };
    }
};

template<class I, class F>
struct FlatMap : DefaultInClass<FlatMap<I, F>, Iterator> {
    using Mapped                              = mtp::invoke_result_t<F&, typename I::Item>;
    using MappedIterator                      = Map<I, F>;
    using Flattened                           = Flatten<MappedIterator>;
    using Item                                = typename Flattened::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED = Impled<Flattened, DoubleEndedIterator>;
    static constexpr bool PROVEN_FUSED        = Impled<MappedIterator, FusedIterator>;

    Flattened inner;

    FlatMap(I in, F fn): inner(MappedIterator(rstd::move(in), rstd::move(fn))) {}

    auto next() -> Option<Item> { return inner.next(); }

    auto next_back() -> Option<Item>
        requires requires(Flattened& flattened) { flattened.next_back(); }
    {
        return inner.next_back();
    }

    auto size_hint() const -> SizeHint { return inner.size_hint(); }
};

// Borrows an iterator by pointer so it can be partially consumed without moving.
template<class I>
struct ByRef : DefaultInClass<ByRef<I>, Iterator> {
    using Item                                = typename I::Item;
    static constexpr bool PROVEN_DOUBLE_ENDED = Impled<I, DoubleEndedIterator>;
    static constexpr bool PROVEN_EXACT_SIZE   = Impled<I, ExactSizeIterator>;
    static constexpr bool PROVEN_FUSED        = Impled<I, FusedIterator>;
    static constexpr bool PROVEN_TRUSTED_LEN  = Impled<I, TrustedLen>;
    I*                    inner;
    explicit ByRef(I* p [[clang::lifetimebound]]): inner(p) {}
    auto next() -> Option<Item> { return inner->next(); }
    auto size_hint() const -> SizeHint { return inner->size_hint(); }
    auto next_back() -> Option<Item>
        requires Impled<I, DoubleEndedIterator>
    {
        return as<DoubleEndedIterator>(*inner).next_back();
    }
    auto len() const -> usize
        requires Impled<I, ExactSizeIterator>
    {
        return as<ExactSizeIterator>(*inner).len();
    }
};

template<class I>
struct Intersperse : DefaultInClass<Intersperse<I>, Iterator> {
    using Item                         = typename I::Item;
    static constexpr bool PROVEN_FUSED = Impled<I, FusedIterator>;
    Fuse<I>               i;
    Item                  sep;
    Option<Item>          next_item;
    bool                  started;
    Intersperse(I in, Item s)
        : i(Fuse<I>(rstd::move(in))), sep(rstd::move(s)), next_item(rstd::None()), started(false) {}
    auto next() -> Option<Item> {
        if (! started) {
            started = true;
            return i.next();
        }
        if (next_item.is_some()) return next_item.take();
        auto item = i.next();
        if (item.is_none()) return rstd::None();
        next_item = rstd::move(item);
        return rstd::Some(as<clone::Clone>(sep).clone());
    }
    auto size_hint() const -> SizeHint {
        auto hint      = i.size_hint();
        auto calculate = [this](usize length) -> Option<usize> {
            auto raw  = length.to_primitive();
            auto max  = usize::MAX.to_primitive();
            auto base = raw - ((! started && raw != 0) ? 1 : 0);
            if (next_item.is_some()) {
                if (base == max) return rstd::None();
                ++base;
            }
            if (base > max - raw) return rstd::None();
            return rstd::Some(usize(base + raw));
        };
        auto lower = calculate(hint.template get<0>()).unwrap_or(usize::MAX);
        auto upper = hint.template get<1>();
        if (upper.is_some()) upper = calculate(*upper);
        return { lower, rstd::move(upper) };
    }
};

namespace details
{

export template<class I>
struct ForwardInPlaceTraits {
    using Inner  = InPlaceTraits<I>;
    using Source = typename Inner::Source;

    static constexpr bool  ENABLED   = Inner::ENABLED;
    static constexpr usize EXPAND_BY = Inner::EXPAND_BY;
    static constexpr usize MERGE_BY  = Inner::MERGE_BY;
};

export template<class I, class F>
struct InPlaceTraits<Map<I, F>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(Map<I, F>& value) { return InPlaceTraits<I>::source(value.i); }
};

export template<class I, class F>
struct InPlaceTraits<MapWhile<I, F>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(MapWhile<I, F>& value) {
        return InPlaceTraits<I>::source(value.i);
    }
};

export template<class I, class P>
struct InPlaceTraits<Filter<I, P>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(Filter<I, P>& value) { return InPlaceTraits<I>::source(value.i); }
};

export template<class I, class F>
struct InPlaceTraits<FilterMap<I, F>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(FilterMap<I, F>& value) {
        return InPlaceTraits<I>::source(value.i);
    }
};

export template<class I>
struct InPlaceTraits<Enumerate<I>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(Enumerate<I>& value) { return InPlaceTraits<I>::source(value.i); }
};

export template<class A, class B>
struct InPlaceTraits<Zip<A, B>> : ForwardInPlaceTraits<A> {
    static decltype(auto) source(Zip<A, B>& value) { return InPlaceTraits<A>::source(value.a); }
};

export template<class I>
struct InPlaceTraits<Take<I>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(Take<I>& value) { return InPlaceTraits<I>::source(value.i); }
};

export template<class I>
struct InPlaceTraits<Skip<I>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(Skip<I>& value) { return InPlaceTraits<I>::source(value.i); }
};

export template<class I, class P>
struct InPlaceTraits<TakeWhile<I, P>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(TakeWhile<I, P>& value) {
        return InPlaceTraits<I>::source(value.i);
    }
};

export template<class I, class P>
struct InPlaceTraits<SkipWhile<I, P>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(SkipWhile<I, P>& value) {
        return InPlaceTraits<I>::source(value.i);
    }
};

export template<class I>
struct InPlaceTraits<Cloned<I>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(Cloned<I>& value) { return InPlaceTraits<I>::source(value.i); }
};

export template<class I>
struct InPlaceTraits<Copied<I>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(Copied<I>& value) { return InPlaceTraits<I>::source(value.i); }
};

export template<class I, class F>
struct InPlaceTraits<Inspect<I, F>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(Inspect<I, F>& value) { return InPlaceTraits<I>::source(value.i); }
};

export template<class I, class St, class F>
struct InPlaceTraits<Scan<I, St, F>> : ForwardInPlaceTraits<I> {
    static decltype(auto) source(Scan<I, St, F>& value) {
        return InPlaceTraits<I>::source(value.i);
    }
};

} // namespace details

} // namespace rstd::iter

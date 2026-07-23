export module rstd.core:iter.sources;
import :num.types;
export import :iter.traits;
export import :ptr;

namespace rstd::iter
{

/// Iterator over `&T` of a contiguous range, yielding `ref<T>`.
export template<class T>
struct SliceIter : DefaultInClass<SliceIter<T>, Iterator> {
    using Item        = ref<T>;
    using raw_pointer = typename ptr<T>::value_type*;

    ptr<T> cur;
    ptr<T> fin;

    constexpr SliceIter(raw_pointer begin [[clang::lifetimebound]],
                        raw_pointer end [[clang::lifetimebound]])
        : cur(ptr<T>::from_raw_parts(begin)), fin(ptr<T>::from_raw_parts(end)) {}

    constexpr SliceIter(ptr<T> begin [[clang::lifetimebound]], ptr<T> end [[clang::lifetimebound]])
        : cur(begin), fin(end) {}

    constexpr auto next() -> Option<Item> {
        if (cur == fin) return rstd::None();
        auto p = cur;
        ++cur;
        return rstd::Some(p.as_ref());
    }

    constexpr auto next_back() -> Option<Item> {
        if (cur == fin) return rstd::None();
        --fin;
        return rstd::Some(fin.as_ref());
    }

    constexpr auto size_hint() const -> SizeHint {
        usize n(static_cast<rstd::size_t>(cur.distance_to(fin)));
        return { n, rstd::Some(n) };
    }

    constexpr auto len() const -> usize {
        return usize(static_cast<rstd::size_t>(cur.distance_to(fin)));
    }
};

/// Iterator over `&mut T` of a contiguous range, yielding `mut_ref<T>`.
export template<class T>
struct SliceIterMut : DefaultInClass<SliceIterMut<T>, Iterator> {
    using Item        = mut_ref<T>;
    using raw_pointer = typename mut_ptr<T>::value_type*;

    mut_ptr<T> cur;
    mut_ptr<T> fin;

    constexpr SliceIterMut(raw_pointer begin [[clang::lifetimebound]],
                           raw_pointer end [[clang::lifetimebound]])
        : cur(mut_ptr<T>::from_raw_parts(begin)), fin(mut_ptr<T>::from_raw_parts(end)) {}

    constexpr SliceIterMut(mut_ptr<T> begin [[clang::lifetimebound]],
                           mut_ptr<T> end [[clang::lifetimebound]])
        : cur(begin), fin(end) {}

    constexpr auto next() -> Option<Item> {
        if (cur == fin) return rstd::None();
        auto p = cur;
        ++cur;
        return rstd::Some(p.as_mut_ref());
    }

    constexpr auto next_back() -> Option<Item> {
        if (cur == fin) return rstd::None();
        --fin;
        return rstd::Some(fin.as_mut_ref());
    }

    constexpr auto size_hint() const -> SizeHint {
        usize n(static_cast<rstd::size_t>(cur.distance_to(fin)));
        return { n, rstd::Some(n) };
    }

    constexpr auto len() const -> usize {
        return usize(static_cast<rstd::size_t>(cur.distance_to(fin)));
    }
};

/// Iterator that yields nothing.
export template<class T>
struct Empty : DefaultInClass<Empty<T>, Iterator> {
    using Item        = T;
    constexpr Empty() = default;
    constexpr auto next() -> Option<Item> { return rstd::None(); }
    constexpr auto size_hint() const -> SizeHint { return { usize(), rstd::Some(usize()) }; }
    constexpr auto len() const -> usize { return usize(); }
};

/// Iterator that yields a single value exactly once.
export template<class T>
struct Once : DefaultInClass<Once<T>, Iterator> {
    using Item = T;
    Option<T> val;
    explicit Once(T v): val(rstd::Some(rstd::move(v))) {}
    auto next() -> Option<Item> { return val.take(); }
    auto size_hint() const -> SizeHint {
        usize n = val.is_some() ? usize(1) : usize();
        return { n, rstd::Some(n) };
    }
    auto len() const -> usize { return val.is_some() ? usize(1) : usize(); }
};

/// Iterator that endlessly repeats a value (clones each time).
export template<class T>
struct Repeat : DefaultInClass<Repeat<T>, Iterator> {
    using Item = T;
    T val;
    explicit Repeat(T v): val(rstd::move(v)) {}
    auto next() -> Option<Item> { return rstd::Some(as<clone::Clone>(val).clone()); }
    auto size_hint() const -> SizeHint { return { usize::MAX, rstd::None() }; }
};

/// Iterator that calls a closure returning `Option<T>` until it yields `None`.
export template<class F>
struct FromFn : DefaultInClass<FromFn<F>, Iterator> {
    using Item = typename decltype(mtp::declval<F&>()())::value_type;
    F f;
    explicit FromFn(F fn): f(rstd::move(fn)) {}
    auto next() -> Option<Item> { return f(); }
};

/// Iterator produced by repeatedly applying `succ` to the previous element.
export template<class T, class F>
struct Successors : DefaultInClass<Successors<T, F>, Iterator> {
    using Item = T;
    Option<T> next_val;
    F         succ;
    Successors(Option<T> first, F f): next_val(rstd::move(first)), succ(rstd::move(f)) {}
    auto next() -> Option<Item> {
        auto cur = next_val.take();
        if (cur.is_some()) next_val = succ(*cur);
        return cur;
    }
};

export template<class T>
constexpr auto empty() -> Empty<T> {
    return Empty<T>();
}

export template<class T>
auto once(T v) -> Once<T> {
    return Once<T>(rstd::move(v));
}

export template<class T>
auto repeat(T v) -> Repeat<T> {
    return Repeat<T>(rstd::move(v));
}

export template<class F>
auto from_fn(F f) -> FromFn<F> {
    return FromFn<F>(rstd::move(f));
}

export template<class T, class F>
auto successors(Option<T> first, F f) -> Successors<T, F> {
    return Successors<T, F>(rstd::move(first), rstd::move(f));
}

/// Iterator over `&T` of a `slice<T>`.
export template<class T>
auto from_slice(slice<T> s [[clang::lifetimebound]]) -> SliceIter<T> {
    auto* p = s.as_raw_ptr();
    if (s.is_empty()) return { p, p };
    return { p, p + s.len().to_primitive() };
}

/// Iterator over `&T` of a C array.
export template<class T, rstd::size_t N>
auto from_array(const T (&arr [[clang::lifetimebound]])[N]) -> SliceIter<T> {
    return { arr, arr + N };
}

/// Iterator over `&mut T` of a C array.
export template<class T, rstd::size_t N>
auto from_array_mut(T (&arr [[clang::lifetimebound]])[N]) -> SliceIterMut<T> {
    return { arr, arr + N };
}

} // namespace rstd::iter

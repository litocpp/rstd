export module rstd.core:iter.sources;
export import :iter.traits;
export import :ptr;

namespace rstd::iter
{

/// Iterator over `&T` of a contiguous range, yielding `ref<T>`.
export template<class T>
struct SliceIter : DefaultInClass<SliceIter<T>, Iterator> {
    using Item = ref<T>;

    const T* cur;
    const T* fin;

    constexpr SliceIter(const T* begin, const T* end): cur(begin), fin(end) {}

    constexpr auto next() -> Option<Item> {
        if (cur == fin) return rstd::None();
        auto* p = cur++;
        return rstd::Some(ref<T>::from_raw_parts(p));
    }

    constexpr auto next_back() -> Option<Item> {
        if (cur == fin) return rstd::None();
        --fin;
        return rstd::Some(ref<T>::from_raw_parts(fin));
    }

    constexpr auto size_hint() const -> SizeHint {
        usize n = static_cast<usize>(fin - cur);
        return { n, rstd::Some(n) };
    }

    constexpr auto len() const -> usize { return static_cast<usize>(fin - cur); }
};

/// Iterator over `&mut T` of a contiguous range, yielding `mut_ref<T>`.
export template<class T>
struct SliceIterMut : DefaultInClass<SliceIterMut<T>, Iterator> {
    using Item = mut_ref<T>;

    T* cur;
    T* fin;

    constexpr SliceIterMut(T* begin, T* end): cur(begin), fin(end) {}

    constexpr auto next() -> Option<Item> {
        if (cur == fin) return rstd::None();
        auto* p = cur++;
        return rstd::Some(mut_ref<T>::from_raw_parts(p));
    }

    constexpr auto next_back() -> Option<Item> {
        if (cur == fin) return rstd::None();
        --fin;
        return rstd::Some(mut_ref<T>::from_raw_parts(fin));
    }

    constexpr auto size_hint() const -> SizeHint {
        usize n = static_cast<usize>(fin - cur);
        return { n, rstd::Some(n) };
    }

    constexpr auto len() const -> usize { return static_cast<usize>(fin - cur); }
};

/// Iterator that yields nothing.
export template<class T>
struct Empty : DefaultInClass<Empty<T>, Iterator> {
    using Item        = T;
    constexpr Empty() = default;
    constexpr auto next() -> Option<Item> { return rstd::None(); }
    constexpr auto size_hint() const -> SizeHint { return { 0, rstd::Some(usize(0)) }; }
    constexpr auto len() const -> usize { return 0; }
};

/// Iterator that yields a single value exactly once.
export template<class T>
struct Once : DefaultInClass<Once<T>, Iterator> {
    using Item = T;
    Option<T> val;
    explicit Once(T v): val(rstd::Some(rstd::move(v))) {}
    auto next() -> Option<Item> { return val.take(); }
    auto size_hint() const -> SizeHint {
        usize n = val.is_some() ? 1 : 0;
        return { n, rstd::Some(n) };
    }
    auto len() const -> usize { return val.is_some() ? 1 : 0; }
};

/// Iterator that endlessly repeats a value (clones each time).
export template<class T>
struct Repeat : DefaultInClass<Repeat<T>, Iterator> {
    using Item = T;
    T val;
    explicit Repeat(T v): val(rstd::move(v)) {}
    auto next() -> Option<Item> { return rstd::Some(as<clone::Clone>(val).clone()); }
    auto size_hint() const -> SizeHint { return { ~usize(0), rstd::None() }; }
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
auto from_slice(slice<T> s) -> SliceIter<T> {
    auto* p = s.as_raw_ptr();
    return { p, p + s.len() };
}

/// Iterator over `&T` of a C array.
export template<class T, usize N>
auto from_array(const T (&arr)[N]) -> SliceIter<T> {
    return { arr, arr + N };
}

/// Iterator over `&mut T` of a C array.
export template<class T, usize N>
auto from_array_mut(T (&arr)[N]) -> SliceIterMut<T> {
    return { arr, arr + N };
}

} // namespace rstd::iter

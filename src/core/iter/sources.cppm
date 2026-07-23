export module rstd.core:iter.sources;
import :num.types;
export import :iter.traits;
export import :ptr;

namespace rstd::iter
{

/// Iterator over `&T` of a contiguous range, yielding `ref<T>`.
export template<class T>
struct SliceIter : DefaultInClass<SliceIter<T>, Iterator> {
    using Item                                = ref<T>;
    using raw_pointer                         = typename ptr<T>::value_type*;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;

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
    using Item                                = mut_ref<T>;
    using raw_pointer                         = typename mut_ptr<T>::value_type*;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;

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
    using Item                                = T;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;
    constexpr Empty()                         = default;
    constexpr auto next() -> Option<Item> { return rstd::None(); }
    constexpr auto next_back() -> Option<Item> { return rstd::None(); }
    constexpr auto size_hint() const -> SizeHint { return { usize(), rstd::Some(usize()) }; }
    constexpr auto len() const -> usize { return usize(); }
};

/// Iterator that yields a single value exactly once.
export template<class T>
struct Once : DefaultInClass<Once<T>, Iterator> {
    using Item                                = T;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;
    Option<T>             val;
    explicit Once(T v): val(rstd::Some(rstd::move(v))) {}
    auto next() -> Option<Item> { return val.take(); }
    auto next_back() -> Option<Item> { return val.take(); }
    auto size_hint() const -> SizeHint {
        usize n = val.is_some() ? usize(1) : usize();
        return { n, rstd::Some(n) };
    }
    auto len() const -> usize { return val.is_some() ? usize(1) : usize(); }
};

/// Iterator that calls a closure once, when its item is requested.
export template<class F>
struct OnceWith : DefaultInClass<OnceWith<F>, Iterator> {
    using Item                                = decltype(mtp::declval<F&&>()());
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_EXACT_SIZE   = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;

    Option<F> function;

    explicit OnceWith(F f): function(rstd::Some(rstd::move(f))) {}

    auto next() -> Option<Item> {
        auto f = function.take();
        if (f.is_none()) return rstd::None();
        decltype(auto) value = rstd::move(*f)();
        return rstd::Some<Item>(rstd::forward<Item>(value));
    }

    auto next_back() -> Option<Item> { return next(); }

    auto size_hint() const -> SizeHint {
        auto length = function.is_some() ? usize(1) : usize();
        return { length, rstd::Some(length) };
    }

    auto len() const -> usize { return function.is_some() ? usize(1) : usize(); }
};

/// Iterator that endlessly repeats a value (clones each time).
export template<class T>
struct Repeat : DefaultInClass<Repeat<T>, Iterator> {
    using Item                                = T;
    static constexpr bool PROVEN_DOUBLE_ENDED = true;
    static constexpr bool PROVEN_FUSED        = true;
    static constexpr bool PROVEN_TRUSTED_LEN  = true;
    T                     val;
    explicit Repeat(T v): val(rstd::move(v)) {}
    auto next() -> Option<Item> { return rstd::Some(as<clone::Clone>(val).clone()); }
    auto next_back() -> Option<Item> { return next(); }
    auto size_hint() const -> SizeHint { return { usize::MAX, rstd::None() }; }
};

/// Iterator that calls a closure for every requested item.
export template<class F>
struct RepeatWith : DefaultInClass<RepeatWith<F>, Iterator> {
    using Item                               = decltype(mtp::declval<F&>()());
    static constexpr bool PROVEN_FUSED       = true;
    static constexpr bool PROVEN_TRUSTED_LEN = true;

    F function;

    explicit RepeatWith(F f): function(rstd::move(f)) {}

    auto next() -> Option<Item> {
        decltype(auto) value = function();
        return rstd::Some<Item>(rstd::forward<Item>(value));
    }

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
    using Item                         = T;
    static constexpr bool PROVEN_FUSED = true;
    Option<T>             next_val;
    F                     succ;
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

export template<class F>
auto once_with(F f) -> OnceWith<F> {
    return OnceWith<F>(rstd::move(f));
}

export template<class T>
    requires Impled<T, clone::Clone>
auto repeat(T v) -> Repeat<T> {
    return Repeat<T>(rstd::move(v));
}

export template<class F>
auto repeat_with(F f) -> RepeatWith<F> {
    return RepeatWith<F>(rstd::move(f));
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

namespace rstd
{

template<typename T>
struct Impl<iter::IntoIterator, ref<T[]>> : ImplBase<ref<T[]>> {
    using IntoIter = iter::SliceIter<T>;

    auto into_iter() -> IntoIter { return iter::from_slice(this->self()); }
};

template<typename T>
struct Impl<iter::IntoIterator, mut_ref<T[]>> : ImplBase<mut_ref<T[]>> {
    using IntoIter = iter::SliceIterMut<T>;

    auto into_iter() -> IntoIter {
        auto& source = this->self();
        auto* data   = source.as_raw_ptr();
        return { data, data + source.len().to_primitive() };
    }
};

} // namespace rstd

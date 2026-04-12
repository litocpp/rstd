export module rstd.basic:tuple;
export import :prelude;
export import :mtp;

using namespace rstd;

/// Storage for a single tuple element. Tagged with an index to allow
/// multiple elements of the same type within one tuple.
template<usize I, typename T>
struct tuple_leaf {
    T value;
};

template<typename IndexSeq, typename... Ts>
struct tuple_impl;

template<usize... Is, typename... Ts>
struct tuple_impl<mtp::index_sequence<Is...>, Ts...> : tuple_leaf<Is, Ts>... {
    constexpr tuple_impl() noexcept((mtp::triv_init<Ts> && ...)) = default;

    template<typename... Us>
    constexpr tuple_impl(Us&&... us) noexcept((mtp::noex_init<Ts, Us> && ...))
        : tuple_leaf<Is, Ts>(rstd::forward<Us>(us))... {}
};

/// Type of the I-th element in a parameter pack.
template<usize I, typename... Ts>
struct nth_type;

template<typename T, typename... Ts>
struct nth_type<0, T, Ts...> {
    using type = T;
};

template<usize I, typename T, typename... Ts>
struct nth_type<I, T, Ts...> {
    using type = typename nth_type<I - 1, Ts...>::type;
};

template<usize I, typename... Ts>
using nth_type_t = typename nth_type<I, Ts...>::type;

/// Count occurrences of T in a parameter pack.
template<typename T, typename... Ts>
constexpr usize type_count = (usize(0) + ... + (mtp::same_as<T, Ts> ? usize(1) : usize(0)));

/// Index of the first occurrence of T in a parameter pack.
template<typename T, typename... Ts>
struct type_index;

template<typename T, typename U, typename... Ts>
struct type_index<T, U, Ts...> {
    static constexpr usize value = mtp::same_as<T, U> ? usize(0) : usize(1) + type_index<T, Ts...>::value;
};

template<typename T>
struct type_index<T> {
    static constexpr usize value = 0;
};

template<typename T, typename... Ts>
constexpr usize type_index_v = type_index<T, Ts...>::value;

/// Cast a `tuple_impl` to the leaf base for element `I`, exposing its `value`.
template<usize I, typename T, typename Impl>
constexpr auto& leaf_ref(Impl& impl) noexcept {
    return static_cast<tuple_leaf<I, T>&>(impl).value;
}

template<usize I, typename T, typename Impl>
constexpr auto const& leaf_ref(const Impl& impl) noexcept {
    return static_cast<const tuple_leaf<I, T>&>(impl).value;
}

namespace rstd
{

/// A fixed-size heterogeneous collection of values, analogous to Rust's tuple.
///
/// Supports construction, copy/move, element access via `get<I>()` or the
/// free function `get<I>(t)`, and C++ structured bindings.
export template<typename... Ts>
class tuple {
    using impl_t = tuple_impl<mtp::make_index_sequence<sizeof...(Ts)>, Ts...>;
    impl_t impl;

public:
    /// The number of elements.
    static constexpr usize size = sizeof...(Ts);

    constexpr tuple() noexcept((mtp::triv_init<Ts> && ...)) = default;

    /// Constructs the tuple
    constexpr tuple(Ts const&... ts) noexcept((mtp::noex_init<Ts, Ts> && ...))
        : impl(rstd::forward<Ts>(ts)...) {}

    /// Constructs the tuple from individual element values.
    template<typename... Us>
        requires(sizeof...(Us) == sizeof...(Ts)) && (sizeof...(Us) > 0)
    constexpr tuple(Us&&... us) noexcept((mtp::noex_init<Ts, Us> && ...))
        : impl(rstd::forward<Us>(us)...) {}

    constexpr tuple(const tuple&)            = default;
    constexpr tuple(tuple&&) noexcept        = default;
    constexpr tuple& operator=(const tuple&) = default;
    constexpr tuple& operator=(tuple&&)      = default;

    /// Access the I-th element (lvalue overload).
    template<usize I>
    constexpr auto& get() & noexcept {
        static_assert(I < sizeof...(Ts), "tuple index out of range");
        using T = nth_type_t<I, Ts...>;
        return leaf_ref<I, T>(impl);
    }

    /// Access the I-th element (const lvalue overload).
    template<usize I>
    constexpr auto const& get() const& noexcept {
        static_assert(I < sizeof...(Ts), "tuple index out of range");
        using T = nth_type_t<I, Ts...>;
        return leaf_ref<I, T>(impl);
    }

    /// Access the I-th element (rvalue overload).
    template<usize I>
    constexpr auto&& get() && noexcept {
        static_assert(I < sizeof...(Ts), "tuple index out of range");
        using T = nth_type_t<I, Ts...>;
        return static_cast<T&&>(leaf_ref<I, T>(impl));
    }

    /// Access the element of type T (lvalue overload).
    template<typename T>
    constexpr T& get() & noexcept {
        static_assert(type_count<T, Ts...> == 1,
                      "tuple type-based get requires exactly one element of the requested type");
        return leaf_ref<type_index_v<T, Ts...>, T>(impl);
    }

    /// Access the element of type T (const lvalue overload).
    template<typename T>
    constexpr T const& get() const& noexcept {
        static_assert(type_count<T, Ts...> == 1,
                      "tuple type-based get requires exactly one element of the requested type");
        return leaf_ref<type_index_v<T, Ts...>, T>(impl);
    }

    /// Access the element of type T (rvalue overload).
    template<typename T>
    constexpr T&& get() && noexcept {
        static_assert(type_count<T, Ts...> == 1,
                      "tuple type-based get requires exactly one element of the requested type");
        return static_cast<T&&>(leaf_ref<type_index_v<T, Ts...>, T>(impl));
    }
};

/// Free function version of `get<I>(t)`, for structured-binding compatibility.
export template<usize I, typename... Ts>
constexpr auto& get(tuple<Ts...>& t) noexcept {
    return t.template get<I>();
}

export template<usize I, typename... Ts>
constexpr auto const& get(const tuple<Ts...>& t) noexcept {
    return t.template get<I>();
}

export template<usize I, typename... Ts>
constexpr auto&& get(tuple<Ts...>&& t) noexcept {
    return rstd::move(t).template get<I>();
}

export template<typename T, typename... Ts>
constexpr T& get(tuple<Ts...>& t) noexcept {
    return t.template get<T>();
}

export template<typename T, typename... Ts>
constexpr T const& get(const tuple<Ts...>& t) noexcept {
    return t.template get<T>();
}

export template<typename T, typename... Ts>
constexpr T&& get(tuple<Ts...>&& t) noexcept {
    return rstd::move(t).template get<T>();
}

/// Creates a tuple, deducing element types from the arguments.
export template<typename... Ts>
constexpr auto make_tuple(Ts&&... ts) {
    return tuple<mtp::rm_cvf<Ts>...>(rstd::forward<Ts>(ts)...);
}

// ── apply: invoke a callable with tuple elements unpacked ───────────────
namespace tuple_detail
{
template<typename F, typename Tup, usize... Is>
constexpr decltype(auto) apply_impl(F&& f, Tup&& t, mtp::index_sequence<Is...>) {
    return rstd::forward<F>(f)(rstd::get<Is>(rstd::forward<Tup>(t))...);
}
} // namespace tuple_detail

/// Invokes `f` with the elements of `t` as individual arguments.
export template<typename F, typename... Ts>
constexpr decltype(auto) apply(F&& f, tuple<Ts...>& t) {
    return tuple_detail::apply_impl(
        rstd::forward<F>(f), t, mtp::make_index_sequence<sizeof...(Ts)> {});
}

export template<typename F, typename... Ts>
constexpr decltype(auto) apply(F&& f, const tuple<Ts...>& t) {
    return tuple_detail::apply_impl(
        rstd::forward<F>(f), t, mtp::make_index_sequence<sizeof...(Ts)> {});
}

export template<typename F, typename... Ts>
constexpr decltype(auto) apply(F&& f, tuple<Ts...>&& t) {
    return tuple_detail::apply_impl(
        rstd::forward<F>(f), rstd::move(t), mtp::make_index_sequence<sizeof...(Ts)> {});
}

// ── Equality comparison ──────────────────────────────────────────────────
template<usize I, typename... Ts>
constexpr bool tuple_eq(const tuple<Ts...>& a, const tuple<Ts...>& b) {
    if constexpr (I == sizeof...(Ts)) {
        return true;
    } else {
        return (a.template get<I>() == b.template get<I>()) && tuple_eq<I + 1>(a, b);
    }
}

export template<typename... Ts>
constexpr bool operator==(const tuple<Ts...>& a, const tuple<Ts...>& b) {
    return tuple_eq<0>(a, b);
}

} // namespace rstd

// ── Structured binding support ───────────────────────────────────────────
namespace std
{

template<typename... Ts>
struct tuple_size<::rstd::tuple<Ts...>> : integral_constant<::rstd::usize, sizeof...(Ts)> {};

template<::rstd::usize I, typename... Ts>
struct tuple_element<I, ::rstd::tuple<Ts...>> {
    using type = ::nth_type_t<I, Ts...>;
};

} // namespace std

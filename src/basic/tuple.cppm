export module rstd.basic:tuple;
export import :prelude;
export import :mtp;
export import :mtp.std;

using namespace rstd;

/// Storage for a single tuple element. Tagged with an index to allow
/// multiple elements of the same type within one tuple.
template<usize I, typename T>
struct tuple_leaf {
    T value;

    constexpr tuple_leaf() noexcept = default;

    template<typename U>
        requires(! mtp::same<mtp::rm_cvf<U>, tuple_leaf>)
    constexpr tuple_leaf(U&& v) noexcept(noexcept(T(rstd::forward<U>(v))))
        : value(rstd::forward<U>(v)) {}
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

/// Cast a `tuple_impl` to the leaf base for element `I`, exposing its `value`.
template<usize I, typename T, typename Impl>
constexpr auto& leaf_ref(Impl& impl) noexcept {
    return static_cast<tuple_leaf<I, T>&>(impl).value;
}

template<usize I, typename T, typename Impl>
constexpr auto const& leaf_ref(const Impl& impl) noexcept {
    return static_cast<const tuple_leaf<I, T>&>(impl).value;

} // namespace detail

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

// ── Equality comparison ──────────────────────────────────────────────────
namespace detail
{
template<usize I, typename... Ts>
constexpr bool tuple_eq(const tuple<Ts...>& a, const tuple<Ts...>& b) {
    if constexpr (I == sizeof...(Ts)) {
        return true;
    } else {
        return (a.template get<I>() == b.template get<I>()) && tuple_eq<I + 1>(a, b);
    }
}
} // namespace detail

export template<typename... Ts>
constexpr bool operator==(const tuple<Ts...>& a, const tuple<Ts...>& b) {
    return detail::tuple_eq<0>(a, b);
}

} // namespace rstd

// ── Structured binding support ───────────────────────────────────────────
namespace std
{

template<typename... Ts>
struct tuple_size<::rstd::tuple<Ts...>> : integral_constant<usize, sizeof...(Ts)> {};

template<::rstd::usize I, typename... Ts>
struct tuple_element<I, ::rstd::tuple<Ts...>> {
    using type = ::nth_type_t<I, Ts...>;
};

} // namespace std

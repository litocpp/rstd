export module rstd.core:clone_tuple;
export import :clone;
export import rstd.basic;

namespace rstd
{

export template<typename... Ts>
class CloneTuple : public DefaultInClass<CloneTuple<Ts...>, clone::Clone> {
    static_assert((Impled<Ts, clone::Clone> && ...),
                  "CloneTuple elements must implement rstd::clone::Clone");

    tuple<Ts...> m_values;

    struct FromTuple {};

    CloneTuple(FromTuple, tuple<Ts...> values): m_values(rstd::move(values)) {}

    static auto clone_values(const tuple<Ts...>& values) -> tuple<Ts...> {
        return rstd::apply(
            [](const auto&... elements) -> tuple<Ts...> {
                return { rstd::as<clone::Clone>(elements).clone()... };
            },
            values);
    }

public:
    static constexpr usize size = sizeof...(Ts);

    template<usize I>
    using element_type = mtp::tuple_element<I, tuple<Ts...>>;

    constexpr CloneTuple()
        requires(mtp::init<Ts> && ...)
    = default;

    template<typename... Us>
        requires(sizeof...(Us) == sizeof...(Ts)) && (sizeof...(Us) > 0) &&
                (mtp::init<Ts, Us &&> && ...)
    constexpr CloneTuple(Us&&... values): m_values(rstd::forward<Us>(values)...) {}

    CloneTuple(const CloneTuple& other): m_values(clone_values(other.m_values)) {}

    auto operator=(const CloneTuple& other) -> CloneTuple&
        requires(mtp::assign_move<Ts> && ...)
    {
        if (this != rstd::addressof(other)) {
            m_values = clone_values(other.m_values);
        }
        return *this;
    }

    CloneTuple(CloneTuple&&)                    = default;
    auto operator=(CloneTuple&&) -> CloneTuple& = default;

    auto clone() const -> CloneTuple {
        return CloneTuple { FromTuple {}, clone_values(m_values) };
    }

    void clone_from(CloneTuple& source)
        requires(mtp::assign_move<Ts> && ...)
    {
        *this = source;
    }

    template<usize I>
    constexpr decltype(auto) get() & noexcept {
        return m_values.template get<I>();
    }

    template<usize I>
    constexpr decltype(auto) get() const& noexcept {
        return m_values.template get<I>();
    }

    template<usize I>
    constexpr decltype(auto) get() && noexcept {
        return rstd::move(m_values).template get<I>();
    }

    template<typename T>
    constexpr decltype(auto) get() & noexcept {
        return m_values.template get<T>();
    }

    template<typename T>
    constexpr decltype(auto) get() const& noexcept {
        return m_values.template get<T>();
    }

    template<typename T>
    constexpr decltype(auto) get() && noexcept {
        return rstd::move(m_values).template get<T>();
    }

    constexpr auto as_tuple() & noexcept -> tuple<Ts...>& { return m_values; }
    constexpr auto as_tuple() const& noexcept -> const tuple<Ts...>& { return m_values; }
    constexpr auto as_tuple() && noexcept -> tuple<Ts...>&& { return rstd::move(m_values); }
};

export template<typename... Ts>
CloneTuple(Ts&&...) -> CloneTuple<mtp::decay<Ts>...>;

export template<typename... Ts>
constexpr auto make_clone_tuple(Ts&&... values) -> CloneTuple<mtp::decay<Ts>...> {
    return CloneTuple<mtp::decay<Ts>...> { rstd::forward<Ts>(values)... };
}

export template<usize I, typename... Ts>
constexpr decltype(auto) get(CloneTuple<Ts...>& values) noexcept {
    return values.template get<I>();
}

export template<usize I, typename... Ts>
constexpr decltype(auto) get(const CloneTuple<Ts...>& values) noexcept {
    return values.template get<I>();
}

export template<usize I, typename... Ts>
constexpr decltype(auto) get(CloneTuple<Ts...>&& values) noexcept {
    return rstd::move(values).template get<I>();
}

export template<typename T, typename... Ts>
constexpr decltype(auto) get(CloneTuple<Ts...>& values) noexcept {
    return values.template get<T>();
}

export template<typename T, typename... Ts>
constexpr decltype(auto) get(const CloneTuple<Ts...>& values) noexcept {
    return values.template get<T>();
}

export template<typename T, typename... Ts>
constexpr decltype(auto) get(CloneTuple<Ts...>&& values) noexcept {
    return rstd::move(values).template get<T>();
}

export template<typename F, typename... Ts>
constexpr decltype(auto) apply(F&& function, CloneTuple<Ts...>& values) {
    return rstd::apply(rstd::forward<F>(function), values.as_tuple());
}

export template<typename F, typename... Ts>
constexpr decltype(auto) apply(F&& function, const CloneTuple<Ts...>& values) {
    return rstd::apply(rstd::forward<F>(function), values.as_tuple());
}

export template<typename F, typename... Ts>
constexpr decltype(auto) apply(F&& function, CloneTuple<Ts...>&& values) {
    return rstd::apply(rstd::forward<F>(function), rstd::move(values).as_tuple());
}

export template<typename... Ts>
constexpr auto operator==(const CloneTuple<Ts...>& lhs, const CloneTuple<Ts...>& rhs) -> bool {
    return lhs.as_tuple() == rhs.as_tuple();
}

} // namespace rstd

namespace std
{

template<typename... Ts>
struct tuple_size<::rstd::CloneTuple<Ts...>> {
    static constexpr ::rstd::usize value = sizeof...(Ts);
};

template<::rstd::usize I, typename... Ts>
struct tuple_element<I, ::rstd::CloneTuple<Ts...>> {
    using type = typename ::rstd::CloneTuple<Ts...>::template element_type<I>;
};

} // namespace std

module;
#include <optional>
#include <memory>
export module rstd.core:option;
export import :basic;
export import :meta;
export import :clone;
export import :fmt;
export import :ops.function;

namespace rstd::option
{

export template<typename T>
class Option;

namespace detail
{
template<typename T>
struct option_traits {};

template<typename T>
struct option_traits<Option<T>> {
    using value_type = T;
    using ret_value_t = T;
    using union_value_t = meta::conditional_t<meta::is_reference_v<T>,
        meta::add_pointer_t<meta::remove_reference_t<T>>,
        std::remove_cv_t<T>>;
};

template<typename T>
struct option_traits<Option<T>&&> : option_traits<Option<T>> {
    using ret_value_t = T&&;
};

template<typename T>
struct option_traits<Option<T>&> : option_traits<Option<T>> {
    using ret_value_t = T&;
};

template<typename T>
struct option_traits<const Option<T>&> : option_traits<Option<T>> {
    using ret_value_t = meta::add_const_t<T>&;
};

template<typename T, typename U>
constexpr void reinit(T* newval, U* oldval, U&& arg) 
    noexcept(std::is_nothrow_constructible_v<T, U>) {
    if constexpr (std::is_nothrow_constructible_v<T, U>) {
        std::destroy_at(oldval);
        std::construct_at(newval, std::forward<U>(arg));
    } else if constexpr (std::is_nothrow_move_constructible_v<T>) {
        T tmp(std::forward<U>(arg));
        std::destroy_at(oldval);
        std::construct_at(newval, std::move(tmp));
    } else {
        std::construct_at(newval, std::forward<U>(arg));
    }
}

template<typename T>
struct option_base : WithTrait<Option<T>, clone::Clone> {
protected:
    constexpr auto cast_self() -> Option<T>& { 
        return static_cast<Option<T>&>(*this); 
    }
    
    constexpr auto cast_self() const -> const Option<T>& {
        return static_cast<const Option<T>&>(*this);
    }

    template<typename V>
    constexpr void construct_val(V&& val) {
        auto& self = cast_self();
        self.m_has_val = true;
        if constexpr (meta::is_reference_v<T>) {
            self.m_val = std::addressof(val);
        } else {
            std::construct_at(std::addressof(self.m_val), std::forward<V>(val));
        }
    }

public:
    constexpr auto is_some() const noexcept -> bool {
        return cast_self().m_has_val;
    }

    constexpr auto is_none() const noexcept -> bool {
        return !cast_self().m_has_val;
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, bool(T)>>
    auto is_some_and(F&& f) -> bool {
        if (is_some()) {
            return std::move(f)(std::move(*cast_self()));
        }
        return false;
    }

    auto expect(str& msg) -> T {
        if (is_some()) {
            return std::move(*cast_self());
        }
        rstd::panic("{}", msg);
        rstd::unreachable();
    }

    auto unwrap() -> T {
        if (is_some()) {
            return std::move(*cast_self());
        }
        rstd::panic("called `Option::unwrap()` on a `None` value");
        rstd::unreachable();
    }

    template<typename U>
    auto unwrap_or(U&& default_value) -> T {
        if (is_some()) {
            return std::move(*cast_self());
        }
        return std::forward<U>(default_value);
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, T()>>
    auto unwrap_or_else(F&& f) -> T {
        if (is_some()) {
            return std::move(*cast_self());
        }
        return std::move(f)();
    }

    template<typename F, typename U = std::invoke_result_t<F, T>>
        requires ImplementedT<FnOnce<F, U(T)>>
    auto map(F&& f) -> Option<U> {
        if (is_some()) {
            return Option<U>(std::move(f)(std::move(*cast_self())));
        }
        return Option<U>();
    }

    [[nodiscard]]
    constexpr const T& operator*() const& noexcept {
        rstd_assert(is_some());
        return *cast_self().m_val;
    }

    [[nodiscard]]
    constexpr T& operator*() & noexcept {
        rstd_assert(is_some());
        return *cast_self().m_val;
    }

    [[nodiscard]]
    constexpr explicit operator bool() const noexcept {
        return is_some();
    }
};

} // namespace detail

template<typename T>
class Option : public detail::option_base<T> {
    template<typename>
    friend class detail::option_base;
    template<template<typename> class, typename>
    friend class rstd::Impl;

    using traits = detail::option_traits<Option<T>>;
    union {
        traits::union_value_t m_val;
    };
    bool m_has_val;

public:
    using value_type = T;

    constexpr Option() noexcept : m_has_val(false) {}

    constexpr Option(T&& val) 
        noexcept(meta::nothrow_constructible<T, T>) {
        this->construct_val(std::forward<T>(val));
    }

    Option(const Option&) = default;
    Option(Option&&) noexcept = default;

    ~Option() = default;

    constexpr ~Option()
        requires(!std::is_trivially_destructible_v<T>)
    {
        if (m_has_val) {
            std::destroy_at(std::addressof(m_val));
        }
    }

    Option& operator=(const Option&) = delete;
    Option& operator=(Option&&) = default;
};

} // namespace rstd::option

namespace rstd
{
export template<typename T>
using Option = option::Option<T>;

export template<template<typename> class T, typename Self>
    requires meta::is_specialization_of_v<T<Self>, clone::Clone> &&
             meta::is_specialization_of_v<Self, rstd::option::Option> &&
             Implemented<typename option::detail::option_traits<Self>::union_value_t, clone::Clone>
struct Impl<T, Self> {
    static auto clone(TraitPtr self) -> Self {
        auto& o = self.as_ref<Self>();
        if (o.is_some()) {
            if constexpr (meta::is_reference_v<typename Self::value_type>) {
                return Option(*Impl<clone::Clone, 
                    typename option::detail::option_traits<Self>::union_value_t>::clone(&o.m_val));
            } else {
                return Option(Impl<clone::Clone, 
                    typename option::detail::option_traits<Self>::union_value_t>::clone(&o.m_val));
            }
        }
        return Option<typename Self::value_type>();
    }
};
} // namespace rstd
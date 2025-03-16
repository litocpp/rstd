module;
#include <memory>
export module rstd.core:result;
export import :basic;
export import :meta;
export import :clone;
export import :fmt;
export import :ops.function;
export import :option;

namespace rstd::result
{

export template<typename T, typename E>
class Result;

struct UnknownOk {
    constexpr bool operator==(const UnknownOk&) const noexcept { return true; }
};
struct UnknownErr {
    constexpr bool operator==(const UnknownErr&) const noexcept { return true; }
};

export template<typename T>
using Ok = Result<T, UnknownErr>;

export template<typename T>
using Err = Result<UnknownOk, T>;

namespace detail
{
template<typename T, typename U, typename V>
constexpr void reinit(T* newval, U* oldval,
                      V&& arg) noexcept(std::is_nothrow_constructible_v<T, V>) {
    if constexpr (std::is_nothrow_constructible_v<T, V>) {
        std::destroy_at(oldval);
        std::construct_at(newval, std::forward<V>(arg));
    } else if constexpr (std::is_nothrow_move_constructible_v<T>) {
        T tmp(std::forward<V>(arg)); // might throw
        std::destroy_at(oldval);
        std::construct_at(newval, std::move(tmp));
    } else {
        // Guard
        std::construct_at(newval, std::forward<V>(arg)); // might throw
    }
}

template<typename T>
struct result_traits {};

template<typename T, typename E>
struct result_traits<Result<T, E>> {
    using value_type = T;
    using error_type = E;

    using ret_value_t = T;
    using ret_error_t = E;

    using union_value_t =
        meta::conditional_t<meta::is_reference_v<T>,
                            meta::add_pointer_t<meta::remove_reference_t<T>>, std::remove_cv_t<T>>;
    using union_error_t = meta::conditional_t<meta::is_reference_v<E>,
                                              meta::add_pointer_t<meta::remove_reference_t<E>>, E>;
};

template<typename T, typename E>
struct result_traits<Result<T, E>&&> : result_traits<Result<T, E>> {
    using ret_value_t = T&&;
    using ret_error_t = E&&;
};

template<typename T, typename E>
struct result_traits<Result<T, E>&> : result_traits<Result<T, E>> {
    using ret_value_t = T&;
    using ret_error_t = E&;
};

template<typename T, typename E>
struct result_traits<const Result<T, E>&> : result_traits<Result<T, E>> {
    using ret_value_t = meta::add_const_t<T>&;
    using ret_error_t = meta::add_const_t<E>&;
};

template<typename T, typename E>
struct result_traits<const Result<T, E>&&> : result_traits<Result<T, E>> {
    using ret_value_t = meta::add_const_t<T>&&;
    using ret_error_t = meta::add_const_t<E>&&;
};

} // namespace detail
} // namespace rstd::result

namespace rstd
{
export template<typename T, typename E>
using Result = result::Result<T, E>;
export template<typename T = Empty>
using Ok = result::Ok<T>;
export template<typename T = Empty>
using Err = result::Err<T>;

export template<template<typename> class T, typename Self>
    requires meta::is_specialization_of_v<T<Self>, clone::Clone> &&
             meta::is_specialization_of_v<Self, rstd::result::Result> &&
             Implemented<typename result::detail::result_traits<Self>::union_value_t,
                         clone::Clone> &&
             Implemented<typename result::detail::result_traits<Self>::union_error_t, clone::Clone>
struct Impl<T, Self> {
    using traits = result::detail::result_traits<Self>;
    using v_t    = typename traits::value_type;
    using e_t    = typename traits::error_type;
    using uv_t   = typename traits::union_value_t;
    using ue_t   = typename traits::union_error_t;

    static auto clone(TraitPtr self) -> Self {
        auto& r = self.as_ref<Self>();
        if (r.is_ok()) {
            if constexpr (meta::is_reference_v<v_t>) {
                return Ok(*Impl<clone::Clone, uv_t>::clone(&r.m_val));
            } else {
                return Ok(Impl<clone::Clone, uv_t>::clone(&r.m_val));
            }
        } else {
            if constexpr (meta::is_reference_v<e_t>) {
                return Err(*Impl<clone::Clone, ue_t>::clone(&r.m_err));
            } else {
                return Err(Impl<clone::Clone, ue_t>::clone(&r.m_err));
            }
        }
    }

    static void clone_from(TraitPtr self, Self& source) {
        if (source.is_ok()) {
            if constexpr (meta::is_reference_v<v_t>) {
                self.as_ref<Self>()._assign_val(source.template _get<0>());
            } else {
                self.as_ref<Self>()._assign_val(Impl<clone::Clone, ue_t>::clone(&source.m_val));
            }
        } else {
            if constexpr (meta::is_reference_v<e_t>) {
                self.as_ref<Self>()._assign_err(source.template _get<1>());
            } else {
                self.as_ref<Self>()._assign_err(Impl<clone::Clone, ue_t>::clone(&source.m_err));
            }
        }
    }
};
} // namespace rstd

namespace rstd::result
{

template<fmt::formattable E>
auto unwrap_failed(str& msg, E& e) {
    rstd::panic("{}: {}", msg, e);
}

namespace detail
{

template<typename T, typename E>
struct result_base : WithTrait<Result<T, E>, clone::Clone> {
    template<typename, typename>
    friend class result::Result;

protected:
    constexpr auto _cast() -> Result<T, E>& { return static_cast<Result<T, E>&>(*this); }
    constexpr auto _cast() const -> const Result<T, E>& {
        return static_cast<const Result<T, E>&>(*this);
    }

    template<i32 I, typename U>
        requires meta::same_as<meta::remove_cvref_t<U>, Result<T, E>>
    static constexpr decltype(auto) _get(U&& self) {
        using traits = detail::result_traits<decltype(self)>;
        if constexpr (I == 0) {
            if constexpr (std::is_reference_v<T>) {
                return static_cast<traits::ret_value_t>(*(self.m_val));
            } else {
                return static_cast<traits::ret_value_t>(self.m_val);
            }
        } else {
            if constexpr (std::is_reference_v<E>) {
                return static_cast<traits::ret_error_t>(*(self.m_err));
            } else {
                return static_cast<traits::ret_error_t>(self.m_err);
            }
        }
    }

    template<i32 I>
    constexpr decltype(auto) _get() & {
        return result_base::template _get<I>(static_cast<Result<T, E>&>(*this));
    }

    template<i32 I>
    constexpr decltype(auto) _get() && {
        return result_base::template _get<I>(static_cast<Result<T, E>&&>(*this));
    }

    template<i32 I>
    constexpr decltype(auto) _get() const& {
        return result_base::template _get<I>(static_cast<const Result<T, E>&>(*this));
    }
    template<i32 I>
    constexpr decltype(auto) _get() const&& {
        return result_base::template _get<I>(static_cast<const Result<T, E>&&>(*this));
    }

    template<i32 I>
    constexpr decltype(auto) _get_move() {
        return result_base::template _get<I>(static_cast<Result<T, E>&&>(*this));
    }

    template<i32 I, typename U>
    constexpr decltype(auto) _self_get(U&& self) const {
        return result_base::template _get<I>(std::forward<U>(self));
    }

    template<typename V>
    constexpr void _construct_val(V&& val) {
        auto& self     = _cast();
        self.m_has_val = true;
        if constexpr (meta::is_reference_v<T>) {
            self.m_val = std::addressof(val);
        } else {
            std::construct_at(std::addressof(self.m_val), std::forward<V>(val));
        }
    }
    template<typename V>
    constexpr void _construct_err(V&& err) {
        auto& self     = _cast();
        self.m_has_val = false;
        if constexpr (meta::is_reference_v<E>) {
            self.m_err = std::addressof(err);
        } else {
            std::construct_at(std::addressof(self.m_err), std::forward<V>(err));
        }
    }

    template<typename V>
    constexpr void _assign_val(V&& v) {
        auto& self = _cast();
        if constexpr (meta::is_reference_v<T>) {
            if (self.m_has_val)
                self.m_val = std::addressof(v);
            else {
                detail::reinit(
                    std::addressof(self.m_val), std::addressof(self.m_err), std::addressof(v));
                self.m_has_val = true;
            }
        } else {
            if (self.m_has_val)
                self.m_val = std::forward<V>(v);
            else {
                detail::reinit(
                    std::addressof(self.m_val), std::addressof(self.m_err), std::forward<V>(v));
                self.m_has_val = true;
            }
        }
    }

    template<typename V>
    constexpr void _assign_err(V&& v) {
        auto& self = _cast();
        if constexpr (meta::is_reference_v<E>) {
            if (self.m_has_val) {
                detail::reinit(
                    std::addressof(self.m_err), std::addressof(self.m_val), std::addressof(v));
                self.m_has_val = false;
            } else
                self.m_err = std::addressof(v);
        } else {
            if (self.m_has_val) {
                detail::reinit(
                    std::addressof(self.m_err), std::addressof(self.m_val), std::forward<V>(v));
                self.m_has_val = false;
            } else
                self.m_err = std::forward<V>(v);
        }
    }

public:
    constexpr auto is_ok() const noexcept -> bool {
        auto& self = _cast();
        return self.m_has_val;
    }
    constexpr auto is_err() const noexcept -> bool {
        auto& self = _cast();
        return ! self.m_has_val;
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, bool(T)>>
    auto is_ok_and(F&& f) -> bool {
        if (is_ok()) {
            return std::move(f)(_get_move<0>());
        } else
            return false;
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, bool(E)>>
    auto is_err_and(F&& f) -> bool {
        if (is_err()) {
            return std::move(f)(_get_move<1>());
        } else
            return false;
    }

    auto ok() -> Option<T> {
        if (is_ok()) {
            return _get_move<0>();
        } else
            return rstd::None();
    }

    auto err() -> Option<E> {
        if (is_err()) {
            return _get_move<1>();
        } else
            return rstd::None();
    }

    template<typename F, typename U = std::invoke_result_t<F, T>>
        requires ImplementedT<FnOnce<F, U(T)>>
    auto map(F&& op) -> Result<U, E> {
        if (is_ok()) {
            return Ok(std::move(op)(_get_move<0>()));
        } else {
            return Err(_get<1>());
        }
    }

    template<typename U, typename F>
        requires ImplementedT<FnOnce<F, U(T)>>
    auto map_or(U&& def, F&& op) -> U {
        if (is_ok()) {
            return std::move(op)(_get_move<0>());
        } else {
            return def;
        }
    }

    template<typename U, typename D, typename F>
        requires ImplementedT<FnOnce<D, U(E)>, FnOnce<F, U(T)>>
    auto map_or_else(D&& def, F&& f) -> U {
        if (is_ok()) {
            return std::move(f)(_get_move<0>());
        } else {
            return std::move(def)(_get_move<1>());
        }
    }
    template<typename F, typename O>
        requires ImplementedT<FnOnce<O, F(E)>>
    auto map_err(O&& op) -> Result<T, F> {
        if (is_ok()) {
            return Ok(_get_move<0>());
        } else {
            return Err(std::move(op)(_get_move<1>()));
        }
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, void(T&)>>
    auto inspect(F&& f) -> Result<T, E> {
        if (is_ok()) {
            std::move(f)(_get<0>());
        }
        auto& self = _cast();
        return std::move(self);
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, void(E&)>>
    auto inspect_err(F&& f) -> Result<T, E> {
        if (is_err()) {
            std::move(f)(_get<0>());
        }
        auto& self = _cast();
        return std::move(self);
    }

    auto expect(str& msg) -> T
        requires fmt::formattable<E>
    {
        if (is_ok()) {
            return _get_move<0>();
        } else {
            unwrap_failed(msg, _get<1>());
            rstd::unreachable();
        }
    }

    auto unwrap() -> T
        requires fmt::formattable<E>
    {
        if (is_ok()) {
            return _get_move<0>();
        } else {
            unwrap_failed("called `Result::unwrap()` on an `Err` value", _get<1>());
            rstd::unreachable();
        }
    }

    auto unwrap_or_default() -> T
        requires meta::is_default_constructible_v<T>
    {
        if (is_ok()) {
            return _get_move<0>();
        } else {
            return {};
        }
    }

    auto expect_err(str& msg) -> E
        requires fmt::formattable<T>
    {
        if (is_ok()) {
            unwrap_failed(msg, _get<0>());
            rstd::unreachable();
        } else {
            return _get_move<1>();
        }
    }

    auto unwrap_err() -> E
        requires fmt::formattable<T>
    {
        if (is_err()) {
            return _get_move<0>();
        } else {
            unwrap_failed("called `Result::unwrap_err()` on an `Ok` value", _get<1>());
            rstd::unreachable();
        }
    }

    template<typename U  = void, typename F,
             typename U2 = typename meta::invoke_result_t<F, T>::value_type>
        requires ImplementedT<
            FnOnce<F, Result<meta::conditional_t<meta::is_void_v<U>, U2, U>, E>(T)>>
    auto and_then(F&& op) -> Result<meta::conditional_t<meta::is_void_v<U>, U2, U>, E> {
        if (is_ok()) {
            return std::move(op)(_get_move<0>());
        } else {
            return Err(_get_move<1>());
        }
    }

    template<typename F>
    auto or_(Result<T, F>&& res) -> Result<T, F> {
        if (is_ok()) {
            return Ok(_get_move<0>());
        } else {
            auto& self = _cast();
            return std::move(res);
        }
    }

    template<typename F, typename O>
        requires ImplementedT<FnOnce<O, Result<T, F>(E)>>
    auto or_else(O&& op) -> Result<T, F> {
        if (is_ok()) {
            return Ok(_get_move<0>());
        } else {
            return std::move(op)(_get_move<1>());
        }
    }

    auto unwrap_or(T&& def) -> T {
        if (is_ok()) {
            return _get_move<0>();
        } else {
            return std::move(def);
        }
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, T(E)>>
    auto unwrap_or_else(F&& op) -> T {
        if (is_ok()) {
            return _get_move<0>();
        } else {
            return std::move(op)(_get_move<1>());
        }
    }

    auto unwrap_unchecked() -> T {
        if (is_ok()) {
            return _get_move<0>();
        } else {
            rstd::unreachable();
        }
    }

    auto unwrap_err_unchecked() -> E {
        if (is_err()) {
            return _get_move<1>();
        } else {
            rstd::unreachable();
        }
    }

    constexpr auto as_ref() const -> Result<std::add_lvalue_reference_t<std::add_const_t<T>>,
                                            std::add_lvalue_reference_t<std::add_const_t<E>>> {
        if (is_ok()) {
            return Ok(_get<0>());
        } else {
            return Err(_get<1>());
        }
    }

    [[nodiscard]]
    constexpr const meta::remove_reference_t<T>* operator->() const noexcept {
        rstd_assert(is_ok());
        return std::addressof(_get<0>());
    }

    [[nodiscard]]
    constexpr meta::remove_reference_t<T>* operator->() noexcept {
        rstd_assert(is_ok());
        return std::addressof(_get<0>());
    }

    [[nodiscard]]
    constexpr const T& operator*() const& noexcept {
        rstd_assert(is_ok());
        return _get<0>();
    }

    [[nodiscard]]
    constexpr T& operator*() & noexcept {
        rstd_assert(is_ok());
        return _get<0>();
    }

    [[nodiscard]]
    constexpr const T&& operator*() const&& noexcept {
        rstd_assert(is_ok());
        return _get<0>();
    }

    [[nodiscard]]
    constexpr T&& operator*() && noexcept {
        rstd_assert(is_ok());
        return _get<0>();
    }

    [[nodiscard]]
    constexpr explicit operator bool() const noexcept {
        return is_ok();
    }
};

template<typename T, typename E>
struct result_impl : result_base<T, E> {};

template<typename T, typename E>
struct result_impl<const T&, E> : result_base<const T&, E> {
    constexpr auto copied() -> Result<T, E> {
        if (this->is_ok()) {
            return Ok(T(this->template _self_get<0>()));
        } else {
            return Err(E(this->template _self_get<1>()));
        }
    }

    auto cloned() -> Result<T, E>
        requires Implemented<T, clone::Clone>
    {
        return this->map([](auto t) {
            return Impl<clone::Clone, T>::clone(t);
        });
    }
};

template<typename T, typename E>
struct result_impl<T&, E> : result_base<T&, E> {
    constexpr auto copied() -> Result<T, E> {
        if (this->is_ok()) {
            return Ok(T(this->template _self_get<0>()));
        } else {
            return Err(E(this->template _self_get<1>()));
        }
    }

    auto cloned() -> Result<T, E>
        requires Implemented<T, clone::Clone>
    {
        return this->map([](auto t) {
            return Impl<clone::Clone, T>::clone(t);
        });
    }
};

template<typename T, typename E>
struct result_impl<Result<T, E>, E> : result_base<Result<T, E>, E> {
    constexpr auto flatten() -> Result<T, E> {
        if (this->is_ok()) {
            return this->template _self_get<0>();
        } else {
            return Err(this->template _self_get<1>());
        }
    }
};

} // namespace detail

template<typename T, typename E>
class Result : public detail::result_impl<T, E> {
    template<typename, typename>
    friend class detail::result_base;
    template<template<typename> class, typename>
    friend class rstd::Impl;

    using traits = detail::result_traits<Result<T, E>>;
    union {
        traits::union_value_t m_val;
        traits::union_error_t m_err;
    };
    bool m_has_val;

public:
    using value_type = T;
    using error_type = E;

    template<typename U>
    using rebind = Result<U, error_type>;

    constexpr Result() noexcept(std::is_nothrow_default_constructible_v<T>)
        requires std::is_default_constructible_v<T>
        : m_val(), m_has_val(true) {}

    // Ok ctor
    Result(T&& val) noexcept(meta::nothrow_constructible<T, T>)
        requires meta::same_as<E, UnknownErr>
    {
        this->_construct_val(std::forward<T>(val));
    }

    // Err ctor
    Result(E&& err) noexcept(meta::nothrow_constructible<E, E>)
        requires meta::same_as<T, UnknownOk>
    {
        this->_construct_err(std::forward<E>(err));
    }

    // from Ok
    template<typename U>
    Result(U&& o) noexcept(
        meta::nothrow_constructible<T, typename detail::result_traits<U>::value_type>)
        requires meta::constructible_from<UnknownErr,
                                          typename detail::result_traits<U>::error_type> &&
                 meta::constructible_from<T, typename detail::result_traits<U>::value_type>
    {
        this->_construct_val(meta::remove_cvref_t<U>::template _get<0>(std::forward<U>(o)));
    }

    // from Err
    template<typename U>
    Result(U&& o) noexcept(
        meta::nothrow_constructible<E, typename detail::result_traits<U>::error_type>)
        requires meta::constructible_from<UnknownOk,
                                          typename detail::result_traits<U>::value_type> &&
                 meta::constructible_from<E, typename detail::result_traits<U>::error_type>
    {
        this->_construct_err(meta::remove_cvref_t<U>::template _get<1>(std::forward<U>(o)));
    }

    Result(const Result&)     = default;
    Result(Result&&) noexcept = default;

    constexpr Result(Result&& o) noexcept(
        meta::conjunction_v<meta::is_nothrow_move_constructible<T>,
                            meta::is_nothrow_move_constructible<E>>)
        requires meta::custom_move_constructible<T> || meta::custom_move_constructible<E>
    {
        if (o.m_has_val) {
            this->_construct_val(Result::template _get<0>(std::move(o)));
        } else {
            this->_construct_err(Result::template _get<1>(std::move(o)));
        }
    }

    constexpr ~Result() = default;

    constexpr ~Result()
        requires(! std::is_trivially_destructible_v<T>) || (! std::is_trivially_destructible_v<E>)
    {
        if (m_has_val) {
            if constexpr (! std::is_trivially_destructible_v<T>) {
                std::destroy_at(std::addressof(m_val));
            }
        } else {
            if constexpr (! std::is_trivially_destructible_v<E>) {
                std::destroy_at(std::addressof(m_err));
            }
        }
    }

    Result& operator=(const Result&) = delete;

    constexpr Result& operator=(Result&& o) noexcept(
        meta::conjunction_v<
            meta::is_nothrow_move_constructible<T>, meta::is_nothrow_move_constructible<E>,
            meta::is_nothrow_move_assignable<T>, meta::is_nothrow_move_assignable<E>>)
        requires meta::is_move_assignable_v<T> && meta::is_move_constructible_v<T> &&
                 meta::is_move_assignable_v<E> && meta::is_move_constructible_v<E>
    {
        if (o.m_has_val)
            this->_assign_val(Result::template _get<0>(std::move(o)));
        else
            this->_assign_err(Result::template _get<1>(std::move(o)));
        return *this;
    }

    template<typename U, typename E2>
        requires(! meta::is_void_v<U>) &&
                requires(const T& t, const U& u, const E& e, const E2& e2) {
                    { t == u } -> meta::convertible_to<bool>;
                    { e == e2 } -> meta::convertible_to<bool>;
                }
    friend constexpr bool operator==(const Result& x, const Result<U, E2>& y) noexcept(
        noexcept(bool(x.template _get<0>() == y.template _get<0>())) &&
        noexcept(bool(x.template _get<1>() == y.template _get<1>()))) {
        if (x.is_ok())
            return y.is_ok() && bool(x.template _get<0>() == y.template _get<0>());
        else
            return ! y.is_ok() && bool(x.template _get<1>() == y.template _get<1>());
    }
};

// Ok
template<typename T>
Result(T&) -> Result<T&, UnknownErr>;

// Err
template<typename T>
Result(T&) -> Result<UnknownOk, T&>;
} // namespace rstd::result

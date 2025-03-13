module;
#include <optional>
export module rstd.core:result;
export import :basic;
export import :meta;
export import :clone;
export import :fmt;
export import :ops.function;

namespace rstd::result
{

export template<typename T, typename E>
class Result;

template<typename T = Empty>
class Ok {
    T m_val;
    template<typename, typename>
    friend class Result;

public:
    constexpr Ok(const Ok&) = default;
    constexpr Ok(Ok&&)      = default;

    template<typename F = T>
        requires std::is_constructible_v<F, T>
    constexpr explicit Ok(F&& ok) noexcept(std::is_nothrow_constructible_v<F, T>)
        : m_val(std::forward<F>(ok)) {}

    constexpr Ok& operator=(const Ok&) = default;
    constexpr Ok& operator=(Ok&&)      = default;
};

// CTAD
template<typename T>
Ok(T) -> Ok<T>;

template<typename E = Empty>
class Err {
    E m_val;
    template<typename, typename>
    friend class Result;

public:
    constexpr Err(const Err&) = default;
    constexpr Err(Err&&)      = default;

    template<typename Er = E>
        requires std::is_constructible_v<E, Er>
    constexpr explicit Err(Er&& e) noexcept(std::is_nothrow_constructible_v<E, Er>)
        : m_val(std::forward<Er>(e)) {}

    constexpr Err& operator=(const Err&) = default;
    constexpr Err& operator=(Err&&)      = default;
};

// CTAD
template<typename E>
Err(E) -> Err<E>;

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
struct result_helper;

template<typename T, typename F>
struct result_helper<Result<T, F>> {
    using value_type = T;
    using error_type = F;
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
             Implemented<typename result::detail::result_helper<Self>::value_type, clone::Clone> &&
             Implemented<typename result::detail::result_helper<Self>::error_type, clone::Clone>
struct Impl<T, Self> : DefImpl<T, Self> {
    static auto clone(TraitPtr self) -> Self {
        using v_t = typename result::detail::result_helper<Self>::value_type;
        using e_t = typename result::detail::result_helper<Self>::error_type;

        auto& r = self.as_ref<Self>();
        if (r.is_ok()) {
            return Ok(Impl<clone::Clone, v_t>::clone(&r.m_val));
        } else {
            return Err(Impl<clone::Clone, e_t>::clone(&r.m_err));
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
struct result_base {
protected:
    constexpr auto cast_self() -> Result<T, E>& { return static_cast<Result<T, E>&>(*this); }
    constexpr auto cast_self() const -> const Result<T, E>& {
        return static_cast<const Result<T, E>&>(*this);
    }

    template<typename V>
    constexpr void assign_val(V&& v) {
        auto& self = cast_self();
        if (self.m_has_val)
            self.m_val = std::forward<V>(v);
        else {
            detail::reinit(&(self.m_val), &(self.m_err), std::forward<V>(v));
            self.m_has_val = true;
        }
    }

    template<typename V>
    constexpr void assign_err(V&& v) {
        auto& self = cast_self();
        if (self.m_has_val) {
            detail::reinit(&(self.m_err), &(self.m_val), std::forward<V>(v));
            self.m_has_val = false;
        } else
            self.m_err = std::forward<V>(v);
    }

    constexpr auto _val() const {
        auto& self = cast_self();
        if constexpr (std::is_reference_v<T>) {
            return *(self.m_val);
        } else {
            return self.m_val;
        }
    }
    constexpr auto _val() {
        auto& self = cast_self();
        if constexpr (std::is_reference_v<T>) {
            return *(self.m_val);
        } else {
            return self.m_val;
        }
    }
    constexpr auto _err() const {
        auto& self = cast_self();
        if constexpr (std::is_reference_v<E>) {
            return *(self.m_err);
        } else {
            return self.m_err;
        }
    }
    constexpr auto _err() {
        auto& self = cast_self();
        if constexpr (std::is_reference_v<E>) {
            return *(self.m_err);
        } else {
            return self.m_err;
        }
    }

public:
    constexpr auto is_ok() const noexcept -> bool {
        auto& self = cast_self();
        return self.m_has_val;
    }
    constexpr auto is_err() const noexcept -> bool {
        auto& self = cast_self();
        return ! self.m_has_val;
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, bool(T)>>
    auto is_ok_and(F&& f) -> bool {
        auto& self = cast_self();
        if (self.is_ok()) {
            return std::move(f)(std::move(self.m_val));
        } else
            return false;
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, bool(E)>>
    auto is_err_and(F&& f) -> bool {
        auto& self = cast_self();
        if (is_err()) {
            return std::move(f)(std::move(self.m_err));
        } else
            return false;
    }

    auto ok() -> std::optional<T> {
        auto& self = cast_self();
        if (self.is_ok()) {
            return std::move(self.m_val);
        } else
            return std::nullopt;
    }

    auto err() -> std::optional<E> {
        auto& self = cast_self();
        if (self.is_err()) {
            return std::move(self.m_err);
        } else
            return std::nullopt;
    }

    template<typename F, typename U = std::invoke_result_t<F, T>>
        requires ImplementedT<FnOnce<F, U(T)>>
    auto map(F&& op) -> Result<U, E> {
        auto& self = cast_self();
        if (self.is_ok()) {
            return Ok(std::move(op)(std::move(self.m_val)));
        } else {
            return Err(self.m_err);
        }
    }

    template<typename U, typename F>
        requires ImplementedT<FnOnce<F, U(T)>>
    auto map_or(U&& def, F&& op) -> U {
        auto& self = cast_self();
        if (self.is_ok()) {
            return std::move(op)(std::move(self.m_val));
        } else {
            return def;
        }
    }

    template<typename U, typename D, typename F>
        requires ImplementedT<FnOnce<D, U(E)>, FnOnce<F, U(T)>>
    auto map_or_else(D&& def, F&& f) -> U {
        auto& self = cast_self();
        if (self.is_ok()) {
            return std::move(f)(std::move(self.m_val));
        } else {
            return std::move(def)(std::move(self.m_err));
        }
    }
    template<typename F, typename O>
        requires ImplementedT<FnOnce<O, F(E)>>
    auto map_err(O&& op) -> Result<T, F> {
        auto& self = cast_self();
        if (self.is_ok()) {
            return Ok(std::move(self.m_val));
        } else {
            return Err(std::move(op)(std::move(self.m_err)));
        }
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, void(T&)>>
    auto inspect(F&& f) -> Result<T, E> {
        auto& self = cast_self();
        if (self.is_ok()) {
            std::move(f)(self.m_val);
        }
        return std::move(self);
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, void(E&)>>
    auto inspect_err(F&& f) -> Result<T, E> {
        auto& self = cast_self();
        if (self.is_err()) {
            std::move(f)(self.m_err);
        }
        return std::move(self);
    }

    auto expect(str& msg) -> T
        requires fmt::formattable<E>
    {
        auto& self = cast_self();
        if (self.is_ok()) {
            return std::move(self.m_val);
        } else {
            unwrap_failed(msg, self.m_err);
            return {};
        }
    }

    auto unwrap() -> T
        requires fmt::formattable<E>
    {
        auto& self = cast_self();
        if (self.is_ok()) {
            return std::move(self.m_val);
        } else {
            unwrap_failed("called `Result::unwrap()` on an `Err` value", self.m_err);
            return {};
        }
    }

    auto unwrap_or_default() -> T {
        auto& self = cast_self();
        if (self.is_ok()) {
            return std::move(self.m_val);
        } else {
            return {};
        }
    }

    auto expect_err(str& msg) -> E
        requires fmt::formattable<T>
    {
        auto& self = cast_self();
        if (self.is_ok()) {
            unwrap_failed(msg, self.m_val);
            return {};
        } else {
            return std::move(self.m_err);
        }
    }

    auto unwrap_err() -> E
        requires fmt::formattable<T>
    {
        auto& self = cast_self();
        if (self.is_err()) {
            return std::move(self.m_err);
        } else {
            unwrap_failed("called `Result::unwrap_err()` on an `Ok` value", self.m_val);
            return {};
        }
    }

    template<typename U, typename F>
        requires ImplementedT<FnOnce<F, Result<U, E>(T)>>
    auto and_then(F&& op) -> Result<U, E> {
        auto& self = cast_self();
        if (self.is_ok()) {
            return std::move(op)(std::move(self.m_val));
        } else {
            return Err(std::move(self.m_err));
        }
    }

    template<typename F>
    auto or_(Result<T, F>&& res) -> Result<T, F> {
        auto& self = cast_self();
        if (self.is_ok()) {
            return Ok(std::move(self.m_val));
        } else {
            return std::move(res);
        }
    }

    template<typename F, typename O>
        requires ImplementedT<FnOnce<O, Result<T, F>(E)>>
    auto or_else(O&& op) -> Result<T, F> {
        auto& self = cast_self();
        if (self.is_ok()) {
            return Ok(std::move(self.m_val));
        } else {
            return std::move(op)(std::move(self.m_err));
        }
    }

    auto unwrap_or(T&& def) -> T {
        auto& self = cast_self();
        if (self.is_ok()) {
            return std::move(self.m_val);
        } else {
            return std::move(def);
        }
    }

    template<typename F>
        requires ImplementedT<FnOnce<F, T(E)>>
    auto unwrap_or_else(F&& op) -> T {
        auto& self = cast_self();
        if (self.is_ok()) {
            return std::move(self.m_val);
        } else {
            return std::move(op)(std::move(self.m_err));
        }
    }

    auto unwrap_unchecked() -> T {
        auto& self = cast_self();
        if (self.is_ok()) {
            return std::move(self.m_val);
        } else {
            rstd::unreachable();
        }
    }

    auto unwrap_err_unchecked() -> E {
        auto& self = cast_self();
        if (self.is_err()) {
            return std::move(self.m_err);
        } else {
            rstd::unreachable();
        }
    }

    constexpr auto as_ref() const -> Result<std::add_lvalue_reference_t<std::add_const_t<T>>,
                                            std::add_lvalue_reference_t<std::add_const_t<E>>> {
        auto& self = cast_self();
        if (self.is_ok()) {
            return Ok(self.m_val);
        } else {
            return Err(self.m_err);
        }
    }

    [[nodiscard]]
    constexpr const T* operator->() const noexcept {
        auto& self = cast_self();
        rstd_assert(self.m_has_val);
        return &(self.m_val);
    }

    [[nodiscard]]
    constexpr T* operator->() noexcept {
        auto& self = cast_self();
        rstd_assert(self.m_has_val);
        return &(self.m_val);
    }

    [[nodiscard]]
    constexpr const T& operator*() const& noexcept {
        auto& self = cast_self();
        rstd_assert(self.m_has_val);
        return self.m_val;
    }

    [[nodiscard]]
    constexpr T& operator*() & noexcept {
        auto& self = cast_self();
        rstd_assert(self.m_has_val);
        return self.m_val;
    }

    [[nodiscard]]
    constexpr const T&& operator*() const&& noexcept {
        auto& self = cast_self();
        rstd_assert(self.m_has_val);
        return std::move(self.m_val);
    }

    [[nodiscard]]
    constexpr T&& operator*() && noexcept {
        auto& self = cast_self();
        rstd_assert(self.m_has_val);
        return std::move(self.m_val);
    }

    [[nodiscard]]
    constexpr explicit operator bool() const noexcept {
        auto& self = cast_self();
        return self.m_has_val;
    }
};

template<typename T, typename E>
struct result_impl : result_base<T, E> {};

template<typename T, typename E>
struct result_impl<const T&, E> : result_base<const T&, E> {
    constexpr auto copied() -> Result<T, E> {
        auto& self = this->cast_self();
        if (self.m_has_val) {
            return Result<T, E>::Ok(self.m_val);
        } else {
            return Result<T, E>::Err(self.m_err);
        }
    }

    auto cloned() -> Result<T, E>
        requires Implemented<T, clone::Clone>
    {
        auto& self = this->cast_self();
        return self.map([](auto t) {
            return Impl<clone::Clone, T>::clone(t);
        });
    }
};

template<typename T, typename E>
struct result_impl<T&, E> : result_base<T&, E> {
    constexpr auto copied() -> Result<T, E> {
        auto& self = this->cast_self();
        if (self.m_has_val) {
            return Result<T, E>::Ok(self.m_val);
        } else {
            return Result<T, E>::Err(self.m_err);
        }
    }

    auto cloned() -> Result<T, E>
        requires Implemented<T, clone::Clone>
    {
        auto& self = this->cast_self();
        return self.map([](auto t) {
            return Impl<clone::Clone, T>::clone(t);
        });
    }
};

template<typename T, typename E>
struct result_impl<Result<T, E>, E> : result_base<Result<T, E>, E> {
    constexpr auto flatten() -> Result<T, E> {
        auto& self = this->cast_self();
        if (self.is_ok()) {
            return self.m_val;
        } else {
            return Err(self.m_err);
        }
    }
};

} // namespace detail

template<typename T, typename E>
class Result : public detail::result_impl<T, E>, public WithTrait<Result<T, E>, clone::Clone> {
    template<typename, typename>
    friend class detail::result_base;
    template<typename, typename>
    friend class detail::result_impl;
    template<template<typename> class, typename>
    friend class rstd::Impl;

    using union_value_t =
        meta::conditional_t<meta::is_reference_v<T>,
                            meta::add_pointer_t<meta::remove_reference_t<T>>, std::remove_cv_t<T>>;
    using union_error_t = meta::conditional_t<meta::is_reference_v<E>,
                                              meta::add_pointer_t<meta::remove_reference_t<E>>, E>;

    union {
        union_value_t m_val;
        union_error_t m_err;
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

    Result(Ok<T>&& ok)
        requires(! meta::is_reference_v<T>)
        : m_val(std::move(ok).m_val), m_has_val(true) {}

    Result(Err<E>&& err)
        requires(! meta::is_reference_v<E>)
        : m_err(std::move(err).m_val), m_has_val(false) {}

    Result(Ok<T> ok)
        requires(meta::is_reference_v<T>)
        : m_val(&ok.m_val), m_has_val(true) {}

    Result(Err<E> err)
        requires(meta::is_reference_v<E>)
        : m_err(&err.m_val), m_has_val(false) {}

    Result(const Result&)     = default;
    Result(Result&&) noexcept = default;

    constexpr Result(Result&& o) noexcept(
        meta::conjunction_v<std::is_nothrow_move_constructible<T>,
                            std::is_nothrow_move_constructible<E>>)
        requires std::is_move_constructible_v<T> && std::is_move_constructible_v<E> &&
                 (! std::is_trivially_move_constructible_v<T> ||
                  ! std::is_trivially_move_constructible_v<E>)
        : m_has_val(o.m_has_val) {
        if (m_has_val)
            std::construct_at(&m_val, std::move(o).m_val);
        else
            std::construct_at(&m_err, std::move(o).m_err);
    }

    constexpr ~Result() = default;

    constexpr ~Result()
        requires(! std::is_trivially_destructible_v<T>) || (! std::is_trivially_destructible_v<E>)
    {
        if (m_has_val) {
            if constexpr (std::is_trivially_destructible_v<T>) {
                std::destroy_at(&(m_val));
            }
        } else {
            if constexpr (std::is_trivially_destructible_v<E>) {
                std::destroy_at(&(m_err));
            }
        }
    }

    Result& operator=(const Result&) = delete;

    constexpr Result& operator=(Result&& o) noexcept(
        meta::conjunction_v<std::is_nothrow_move_constructible<T>,
                            std::is_nothrow_move_constructible<E>,
                            std::is_nothrow_move_assignable<T>, std::is_nothrow_move_assignable<E>>)
        requires std::is_move_assignable_v<T> && std::is_move_constructible_v<T> &&
                 std::is_move_assignable_v<E> && std::is_move_constructible_v<E> &&
                 (std::is_nothrow_move_constructible_v<T> ||
                  std::is_nothrow_move_constructible_v<E>)
    {
        if (o.m_has_val)
            this->assign_val(std::move(o.m_val));
        else
            this->assign_err(std::move(o.m_err));
        return *this;
    }

    constexpr bool operator==(const Ok<T>& ok) const noexcept {
        return this->is_ok() && ok.m_val == m_val;
    }
    constexpr bool operator==(const Err<E>& err) const noexcept {
        return this->is_err() && err.m_err == m_err;
    }
};

} // namespace rstd::result

module;
#include <rstd/macro.hpp>
export module rstd.core:result;
export import :clone;
export import :fmt;
export import :ops.function;
export import :option;

namespace rstd::result
{

struct UnknownOk {
    constexpr bool operator==(const UnknownOk&) const noexcept { return true; }
};
struct UnknownErr {
    constexpr bool operator==(const UnknownErr&) const noexcept { return true; }
};

namespace detail
{
template<typename T, typename U, typename V>
constexpr void reinit(T* newval, U* oldval,
                      V&& arg) noexcept(mtp::is_nothrow_constructible_v<T, V>) {
    if constexpr (mtp::is_nothrow_constructible_v<T, V>) {
        rstd::destroy_at(oldval);
        rstd::construct_at(newval, rstd::forward<V>(arg));
    } else if constexpr (mtp::is_nothrow_move_constructible_v<T>) {
        T tmp(rstd::forward<V>(arg)); // might throw
        rstd::destroy_at(oldval);
        rstd::construct_at(newval, rstd::move(tmp));
    } else {
        // Guard
        rstd::construct_at(newval, rstd::forward<V>(arg)); // might throw
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
        mtp::conditional_t<mtp::is_reference_v<T>,
                            mtp::add_pointer_t<mtp::remove_reference_t<T>>, mtp::remove_cv_t<T>>;
    using union_error_t = mtp::conditional_t<mtp::is_reference_v<E>,
                                              mtp::add_pointer_t<mtp::remove_reference_t<E>>, E>;
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
    using ret_value_t = mtp::add_const_t<T>&;
    using ret_error_t = mtp::add_const_t<E>&;
};

template<typename T, typename E>
struct result_traits<const Result<T, E>&&> : result_traits<Result<T, E>> {
    using ret_value_t = mtp::add_const_t<T>&&;
    using ret_error_t = mtp::add_const_t<E>&&;
};

} // namespace detail
} // namespace rstd::result

namespace rstd
{
export using result::Result;
export using result::Ok;
export using result::Err;

template<typename T, typename Self>
    requires mtp::same_as<T, clone::Clone> &&
             mtp::is_specialization_of_v<Self, rstd::result::Result> &&
             Impled<typename result::detail::result_traits<Self>::union_value_t, clone::Clone> &&
             Impled<typename result::detail::result_traits<Self>::union_error_t, clone::Clone>
struct Impl<T, Self> : ImplBase<Self> {
    using traits = result::detail::result_traits<Self>;
    using v_t    = typename traits::value_type;
    using e_t    = typename traits::error_type;
    using uv_t   = typename traits::union_value_t;
    using ue_t   = typename traits::union_error_t;

    auto clone() const -> Self {
        auto& r = this->self();
        if (r.is_ok()) {
            if constexpr (mtp::is_reference_v<v_t>) {
                return Ok(*Impl<clone::Clone, uv_t>::clone(&r.m_val));
            } else {
                return Ok(Impl<clone::Clone, uv_t>::clone(&r.m_val));
            }
        } else {
            if constexpr (mtp::is_reference_v<e_t>) {
                return Err(*Impl<clone::Clone, ue_t> { &r.m_err }.clone());
            } else {
                return Err(Impl<clone::Clone, ue_t> { &r.m_err }.clone());
            }
        }
    }

    void clone_from(Self& source) {
        auto& self = this->self();
        if (source.is_ok()) {
            if constexpr (mtp::is_reference_v<v_t>) {
                self._assign_val(source.template _get<0>());
            } else {
                self._assign_val(Impl<clone::Clone, ue_t> { &source.m_val }.clone());
            }
        } else {
            if constexpr (mtp::is_reference_v<e_t>) {
                self._assign_err(source.template _get<1>());
            } else {
                self._assign_err(Impl<clone::Clone, ue_t> { &source.m_err }.clone());
            }
        }
    }
};
} // namespace rstd

namespace rstd::result
{

template<fmt::formattable E>
auto unwrap_failed(ref<str> msg, E& e) {
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
        requires mtp::same_as<mtp::remove_cvref_t<U>, Result<T, E>>
    static constexpr decltype(auto) _get(U&& self) {
        using traits = detail::result_traits<decltype(self)>;
        if constexpr (I == 0) {
            if constexpr (mtp::is_reference_v<T>) {
                return static_cast<traits::ret_value_t>(*(self.m_val));
            } else {
                return static_cast<traits::ret_value_t>(self.m_val);
            }
        } else {
            if constexpr (mtp::is_reference_v<E>) {
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
        return result_base::template _get<I>(rstd::forward<U>(self));
    }

    template<typename V>
    constexpr void _construct_val(V&& val) {
        auto& self     = _cast();
        self.m_has_val = true;
        if constexpr (mtp::is_reference_v<T>) {
            self.m_val = rstd::addressof(val);
        } else {
            rstd::construct_at(rstd::addressof(self.m_val), rstd::forward<V>(val));
        }
    }
    template<typename V>
    constexpr void _construct_err(V&& err) {
        auto& self     = _cast();
        self.m_has_val = false;
        if constexpr (mtp::is_reference_v<E>) {
            self.m_err = rstd::addressof(err);
        } else {
            rstd::construct_at(rstd::addressof(self.m_err), rstd::forward<V>(err));
        }
    }

    template<typename V>
    constexpr void _assign_val(V&& v) {
        auto& self = _cast();
        if constexpr (mtp::is_reference_v<T>) {
            if (self.m_has_val)
                self.m_val = rstd::addressof(v);
            else {
                detail::reinit(
                    rstd::addressof(self.m_val), rstd::addressof(self.m_err), rstd::addressof(v));
                self.m_has_val = true;
            }
        } else {
            if (self.m_has_val)
                self.m_val = rstd::forward<V>(v);
            else {
                detail::reinit(
                    rstd::addressof(self.m_val), rstd::addressof(self.m_err), rstd::forward<V>(v));
                self.m_has_val = true;
            }
        }
    }

    template<typename V>
    constexpr void _assign_err(V&& v) {
        auto& self = _cast();
        if constexpr (mtp::is_reference_v<E>) {
            if (self.m_has_val) {
                detail::reinit(
                    rstd::addressof(self.m_err), rstd::addressof(self.m_val), rstd::addressof(v));
                self.m_has_val = false;
            } else
                self.m_err = rstd::addressof(v);
        } else {
            if (self.m_has_val) {
                detail::reinit(
                    rstd::addressof(self.m_err), rstd::addressof(self.m_val), rstd::forward<V>(v));
                self.m_has_val = false;
            } else
                self.m_err = rstd::forward<V>(v);
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
    // requires ImpledT<FnOnce<F, bool(T)>>
    auto is_ok_and(F&& f) -> bool {
        if (is_ok()) {
            return rstd::move(f)(_get_move<0>());
        } else
            return false;
    }

    template<typename F>
    // requires ImpledT<FnOnce<F, bool(E)>>
    auto is_err_and(F&& f) -> bool {
        if (is_err()) {
            return rstd::move(f)(_get_move<1>());
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

    template<typename F, typename U = mtp::invoke_result_t<F, T>>
    // requires ImpledT<FnOnce<F, U(T)>>
    auto map(F&& op) -> Result<U, E> {
        if (is_ok()) {
            return Ok(rstd::move(op)(_get_move<0>()));
        } else {
            return Err(_get<1>());
        }
    }

    template<typename U, typename F>
    // requires ImpledT<FnOnce<F, U(T)>>
    auto map_or(U&& def, F&& op) -> U {
        if (is_ok()) {
            return rstd::move(op)(_get_move<0>());
        } else {
            return def;
        }
    }

    template<typename U, typename D, typename F>
    // requires ImpledT<FnOnce<D, U(E)>, FnOnce<F, U(T)>>
    auto map_or_else(D&& def, F&& f) -> U {
        if (is_ok()) {
            return rstd::move(f)(_get_move<0>());
        } else {
            return rstd::move(def)(_get_move<1>());
        }
    }
    template<typename O, typename F = mtp::invoke_result_t<O, E>>
    // requires ImpledT<FnOnce<O, F(E)>>
    auto map_err(O&& op) -> Result<T, F> {
        if (is_ok()) {
            return Ok(_get_move<0>());
        } else {
            return Err(rstd::move(op)(_get_move<1>()));
        }
    }

    template<typename F>
    // requires ImpledT<FnOnce<F, void(T&)>>
    auto inspect(F&& f) -> Result<T, E> {
        if (is_ok()) {
            rstd::move(f)(_get<0>());
        }
        auto& self = _cast();
        return rstd::move(self);
    }

    template<typename F>
    // requires ImpledT<FnOnce<F, void(E&)>>
    auto inspect_err(F&& f) -> Result<T, E> {
        if (is_err()) {
            rstd::move(f)(_get<1>());
        }
        auto& self = _cast();
        return rstd::move(self);
    }

    auto expect(ref<str> msg) -> T
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
        requires mtp::is_default_constructible_v<T>
    {
        if (is_ok()) {
            return _get_move<0>();
        } else {
            return {};
        }
    }

    auto expect_err(ref<str> msg) -> E
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
            return _get_move<1>();
        } else {
            unwrap_failed("called `Result::unwrap_err()` on an `Ok` value", _get<1>());
            rstd::unreachable();
        }
    }

    template<typename U  = void, typename F,
             typename U2 = typename mtp::invoke_result_t<F, T>::value_type>
    // requires ImpledT<FnOnce<F, Result<mtp::conditional_t<mtp::is_void_v<U>, U2, U>, E>(T)>>
    auto and_then(F&& op) -> Result<mtp::conditional_t<mtp::is_void_v<U>, U2, U>, E> {
        if (is_ok()) {
            return rstd::move(op)(_get_move<0>());
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
            return rstd::move(res);
        }
    }

    template<typename F, typename O>
    // requires ImpledT<FnOnce<O, Result<T, F>(E)>>
    auto or_else(O&& op) -> Result<T, F> {
        if (is_ok()) {
            return Ok(_get_move<0>());
        } else {
            return rstd::move(op)(_get_move<1>());
        }
    }

    auto unwrap_or(T&& def) -> T {
        if (is_ok()) {
            return _get_move<0>();
        } else {
            return rstd::move(def);
        }
    }

    template<typename F>
    // requires ImpledT<FnOnce<F, T(E)>>
    auto unwrap_or_else(F&& op) -> T {
        if (is_ok()) {
            return _get_move<0>();
        } else {
            return rstd::move(op)(_get_move<1>());
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

    constexpr auto as_ref() const -> Result<mtp::add_lvalue_reference_t<mtp::add_const_t<T>>,
                                            mtp::add_lvalue_reference_t<mtp::add_const_t<E>>> {
        if (is_ok()) {
            return Ok(_get<0>());
        } else {
            return Err(_get<1>());
        }
    }

    [[nodiscard]]
    constexpr const mtp::remove_reference_t<T>* operator->() const noexcept {
        assert(is_ok());
        return rstd::addressof(_get<0>());
    }

    [[nodiscard]]
    constexpr mtp::remove_reference_t<T>* operator->() noexcept {
        assert(is_ok());
        return rstd::addressof(_get<0>());
    }

    [[nodiscard]]
    constexpr const T& operator*() const& noexcept {
        assert(is_ok());
        return _get<0>();
    }

    [[nodiscard]]
    constexpr T& operator*() & noexcept {
        assert(is_ok());
        return _get<0>();
    }

    [[nodiscard]]
    constexpr const T&& operator*() const&& noexcept {
        assert(is_ok());
        return _get<0>();
    }

    [[nodiscard]]
    constexpr T&& operator*() && noexcept {
        assert(is_ok());
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
        requires Impled<T, clone::Clone>
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
        requires Impled<T, clone::Clone>
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

struct ok_tag {};
struct err_tag {};

} // namespace detail

template<typename T, typename E>
class Result : public detail::result_impl<T, E> {
    template<typename, typename>
    friend struct detail::result_base;
    template<typename, typename>
    friend struct rstd::Impl;

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

    constexpr Result() noexcept(mtp::is_nothrow_default_constructible_v<T>)
        requires mtp::is_default_constructible_v<T>
        : m_val(), m_has_val(true) {}

    // Ok ctor
    constexpr Result(T&& val, detail::ok_tag) noexcept(mtp::nothrow_constructible<T, T>) {
        this->_construct_val(rstd::forward<T>(val));
    }

    // Err ctor
    constexpr Result(E&& err, detail::err_tag) noexcept(mtp::nothrow_constructible<E, E>) {
        this->_construct_err(rstd::forward<E>(err));
    }

    // from Ok
    template<typename U>
    constexpr Result(U&& o) noexcept(
        mtp::nothrow_constructible<T, typename detail::result_traits<U>::value_type>)
        requires mtp::constructible_from<UnknownErr,
                                          typename detail::result_traits<U>::error_type> &&
                 mtp::constructible_from<T, typename detail::result_traits<U>::value_type>
    {
        this->_construct_val(mtp::remove_cvref_t<U>::template _get<0>(rstd::forward<U>(o)));
    }

    // from Err
    template<typename U>
    constexpr Result(U&& o) noexcept(
        mtp::nothrow_constructible<E, typename detail::result_traits<U>::error_type>)
        requires mtp::constructible_from<UnknownOk,
                                          typename detail::result_traits<U>::value_type> &&
                 mtp::constructible_from<E, typename detail::result_traits<U>::error_type>
    {
        this->_construct_err(mtp::remove_cvref_t<U>::template _get<1>(rstd::forward<U>(o)));
    }

    constexpr Result(const Result&) = default;
    constexpr Result(Result&&)      = default;

    constexpr Result(Result&& o) noexcept(
        mtp::conjunction_v<mtp::is_nothrow_move_constructible<T>,
                            mtp::is_nothrow_move_constructible<E>>)
        requires mtp::custom_move_constructible<T> || mtp::custom_move_constructible<E>
    {
        if (o.m_has_val) {
            this->_construct_val(Result::template _get<0>(rstd::move(o)));
        } else {
            this->_construct_err(Result::template _get<1>(rstd::move(o)));
        }
    }

    constexpr ~Result() = default;

    constexpr ~Result()
        requires(! mtp::is_trivially_destructible_v<T>) || (! mtp::is_trivially_destructible_v<E>)
    {
        if (m_has_val) {
            if constexpr (! mtp::is_trivially_destructible_v<T>) {
                rstd::destroy_at(rstd::addressof(m_val));
            }
        } else {
            if constexpr (! mtp::is_trivially_destructible_v<E>) {
                rstd::destroy_at(rstd::addressof(m_err));
            }
        }
    }

    Result& operator=(const Result&) = delete;

    constexpr Result& operator=(Result&&)
        requires mtp::is_trivially_move_assignable_v<typename traits::union_value_t> &&
                     mtp::is_trivially_move_assignable_v<typename traits::union_error_t>
    = default;

    constexpr Result& operator=(Result&& o) noexcept(
        mtp::conjunction_v<mtp::is_nothrow_move_constructible<typename traits::union_value_t>,
                            mtp::is_nothrow_move_constructible<typename traits::union_value_t>,
                            mtp::is_nothrow_move_assignable<typename traits::union_error_t>,
                            mtp::is_nothrow_move_assignable<typename traits::union_error_t>>)
        requires(! (mtp::is_trivially_move_assignable_v<typename traits::union_value_t> &&
                    mtp::is_trivially_move_assignable_v<typename traits::union_error_t>)) &&
                (mtp::is_move_assignable_v<typename traits::union_value_t> &&
                 mtp::is_move_constructible_v<typename traits::union_value_t> &&
                 mtp::is_move_assignable_v<typename traits::union_error_t> &&
                 mtp::is_move_constructible_v<typename traits::union_error_t>)
    {
        if (o.m_has_val)
            this->_assign_val(Result::template _get<0>(rstd::move(o)));
        else
            this->_assign_err(Result::template _get<1>(rstd::move(o)));
        return *this;
    }

    template<typename U, typename E2>
        requires(! mtp::is_void_v<U>) &&
                requires(const T& t, const U& u, const E& e, const E2& e2) {
                    { t == u } -> mtp::convertible_to<bool>;
                    { e == e2 } -> mtp::convertible_to<bool>;
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

template<typename T, typename TErr>
constexpr auto Ok(T&& val) -> Result<T, TErr> {
    return { rstd::forward<T>(val), detail::ok_tag {} };
}

template<typename TErr, typename T>
constexpr auto Err(TErr&& val) -> Result<T, TErr> {
    return { rstd::forward<TErr>(val), detail::err_tag {} };
}
} // namespace rstd::result

namespace rstd::option::detail
{
template<typename T, typename E>
struct option_adapter_l1<result::Result<T, E>> {
    constexpr auto transpose() -> result::Result<Option<T>, E> {
        auto&& self = static_cast<Option<result::Result<T, E>>&&>(*this);
        if (self.is_some()) {
            auto&& t = self.unwrap_unchecked();
            if (t.is_ok()) {
                return Ok(Some(rstd::move(t.unwrap_unchecked())));
            } else {
                return Err(rstd::move(t.unwrap_err_unchecked()));
            }
        } else {
            return Ok<Option<T>>(None());
        }
    }
};

} // namespace rstd::option::detail
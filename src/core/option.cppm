module;
#include <rstd/macro.hpp>
export module rstd.core:option;
export import :clone;
export import :panic;
export import :slice;
export import :ops.function;
export import :assert;
export import :forward;
export import :cmp;

namespace rstd::option
{

export template<typename T>
class Option;

export struct Unknown {};

export template<typename U = void, typename T>
[[gnu::always_inline]] inline constexpr auto Some(T&& val) {
    if constexpr (meta::same_as<U, void>) {
        if constexpr (meta::trivially_value<meta::remove_reference_t<T>>) {
            return Option<meta::remove_reference_t<T>>(rstd::move(val));
        } else {
            return Option<T>(rstd::forward<T>(val));
        }
    } else {
        return Option<U>(rstd::forward<T>(val));
    }
}

export template<typename U = void, typename T = Unknown>
[[gnu::always_inline]] inline constexpr auto None(T&& = {}) {
    if constexpr (meta::same_as<U, void>) {
        if constexpr (meta::same_as<Unknown, T>) {
            return Unknown();
        } else {
            return Option<T>();
        }
    } else {
        return Option<U>();
    }
}

namespace detail
{
template<typename T>
struct option_adapter_l1 {};

template<typename T>
struct option_traits {};

template<typename T>
struct option_traits<Option<T>> {
    using value_type    = T;
    using ret_value_t   = T;
    using union_value_t = meta::conditional_t<meta::is_reference_v<T>,
                                              meta::add_pointer_t<meta::remove_reference_t<T>>, T>;
};

template<typename T>
struct option_traits<Option<T>&&> : option_traits<Option<T>> {
    using ret_value_t = meta::add_rvalue_reference_t<T>;
};

template<typename T>
struct option_traits<Option<T>&> : option_traits<Option<T>> {
    using ret_value_t = meta::add_lvalue_reference_t<T>;
};

template<typename T>
struct option_traits<const Option<T>&> : option_traits<Option<T>> {
    using ret_value_t = meta::add_lvalue_reference_t<meta::add_const_t<T>>;
};

template<typename T>
struct option_traits<const Option<T>&&> : option_traits<Option<T>> {
    using ret_value_t = meta::add_rvalue_reference_t<meta::add_const_t<T>>;
};

export template<typename T>
struct option_store {
    constexpr auto is_some() const noexcept -> bool { return m_has_val; }

protected:
    using union_value_t = T;
    constexpr auto _ptr() const noexcept { return reinterpret_cast<const T*>(m_storage); }
    constexpr auto _ptr() noexcept { return reinterpret_cast<T*>(m_storage); }
    template<typename V>
    constexpr void _construct_val(V&& val) {
        rstd::construct_at(_ptr(), rstd::forward<V>(val));
        m_has_val = true;
    }
    template<typename V>
    constexpr void _assign_val(V&& val) {
        auto ptr = _ptr();
        if (is_some()) {
            if constexpr (meta::is_nothrow_constructible_v<T, V>) {
                rstd::destroy_at(ptr);
                rstd::construct_at(ptr, rstd::forward<V>(val));
            } else if constexpr (meta::is_nothrow_move_constructible_v<T>) {
                T tmp(rstd::forward<V>(val));
                rstd::destroy_at(ptr);
                rstd::construct_at(ptr, rstd::move(tmp));
            } else {
                rstd::destroy_at(ptr);
                rstd::construct_at(ptr, rstd::forward<V>(val));
            }
        } else {
            _construct_val(rstd::forward<V>(val));
            m_has_val = true;
        }
    }
    constexpr void _assign_none() {
        if (is_some()) {
            rstd::destroy_at(_ptr());
            m_has_val = false;
        }
    }

private:
    alignas(T) rstd::byte m_storage[sizeof(T)];
    bool m_has_val { false };
};

template<typename T>
struct option_store<T&> {
    constexpr auto is_some() const noexcept -> bool {
        auto* p = *_ptr();
        return p != nullptr;
    }

protected:
    using union_value_t       = T*;
    using union_const_value_t = T const*;

    constexpr auto _ptr() const noexcept {
        return reinterpret_cast<const union_const_value_t*>(m_storage);
    }
    constexpr auto _ptr() noexcept { return reinterpret_cast<union_value_t*>(m_storage); }
    template<typename V>
    constexpr void _construct_val(V&& val) {
        rstd::construct_at(_ptr(), rstd::addressof(val));
    }
    template<typename V>
    constexpr void _assign_val(V&& val) {
        rstd::construct_at(_ptr(), rstd::addressof(val));
    }
    constexpr void _assign_none() {
        if (is_some()) {
            m_storage = {};
        }
    }

private:
    alignas(T*) rstd::byte m_storage[sizeof(T*)] {};
};

template<typename T>
struct option_base : option_store<T>, WithTrait<Option<T>, clone::Clone> {
    using traits = detail::option_traits<Option<T>>;

protected:
    constexpr auto _cast() -> Option<T>& { return static_cast<Option<T>&>(*this); }

    constexpr auto _cast() const -> const Option<T>& {
        return static_cast<const Option<T>&>(*this);
    }

    template<typename U>
        requires meta::same_as<meta::remove_cvref_t<U>, Option<T>>
    static constexpr decltype(auto) _get(U&& self) {
        using traits = detail::option_traits<decltype(self)>;
        if constexpr (meta::is_reference_v<T>) {
            return static_cast<traits::ret_value_t>(**(self._ptr()));
        } else {
            return static_cast<traits::ret_value_t>(*(self._ptr()));
        }
    }

    constexpr decltype(auto) _get() const& { return _get(static_cast<const Option<T>&>(*this)); }
    constexpr decltype(auto) _get() const&& { return _get(static_cast<const Option<T>&&>(*this)); }
    constexpr decltype(auto) _get() & { return _get(static_cast<Option<T>&>(*this)); }
    constexpr decltype(auto) _get() && { return _get(static_cast<Option<T>&&>(*this)); }
    constexpr decltype(auto) _get_move() { return _get(static_cast<Option<T>&&>(*this)); }

public:
    constexpr auto is_none() const noexcept -> bool { return ! this->is_some(); }

    template<typename F>
    // requires ImpledT<FnOnce<F, bool(T)>>
    auto is_some_and(F&& f) -> bool {
        if (this->is_some()) {
            return rstd::forward<F>(f)(_get_move());
        }
        return false;
    }

    constexpr auto as_ref() const -> Option<meta::add_lvalue_reference_t<meta::add_const_t<T>>> {
        if (this->is_some()) {
            return option::Some(_get());
        } else {
            return option::None();
        }
    }

    auto expect(ref<str> msg) -> T {
        if (this->is_some()) {
            return _get_move();
        }
        rstd::panic("{}", msg);
        rstd::unreachable();
    }

    auto unwrap() -> T {
        if (this->is_some()) {
            return _get_move();
        }
        rstd::panic("called `Option::unwrap()` on a `None` value");
        rstd::unreachable();
    }

    template<typename U>
    auto unwrap_or(U&& default_value) -> T {
        if (this->is_some()) {
            return _get_move();
        }
        return rstd::forward<U>(default_value);
    }

    template<typename F>
    // requires ImpledT<FnOnce<F, T()>>
    auto unwrap_or_else(F&& f) -> T {
        if (this->is_some()) {
            return _get_move();
        }
        return rstd::forward<F>(f)();
    }

    constexpr auto unwrap_unchecked() -> T {
        if (this->is_some()) {
            return _get_move();
        } else {
            rstd::unreachable();
        }
    }

    template<typename F, typename U = meta::invoke_result_t<F, T>>
    // requires ImpledT<FnOnce<F, U(T)>>
    constexpr auto map(F&& f) -> Option<U> {
        if (this->is_some()) {
            return option::Some(rstd::forward<F>(f)(_get_move()));
        }
        return option::None<U>();
    }

    template<typename F, typename U = meta::invoke_result_t<F, T>>
        requires meta::special_of<U, Option>
    constexpr auto and_then(F&& f) -> U {
        if (this->is_some()) {
            return rstd::forward<F>(f)(_get_move());
        }
        return option::None();
    }

    [[nodiscard]]
    constexpr const T& operator*() const& noexcept {
        assert(this->is_some());
        return _get();
    }

    [[nodiscard]]
    constexpr T& operator*() & noexcept {
        assert(this->is_some());
        return _get();
    }

    [[nodiscard]]
    constexpr const meta::remove_reference_t<T>* operator->() const& noexcept {
        assert(this->is_some());
        return rstd::addressof(_get());
    }

    [[nodiscard]]
    constexpr meta::remove_reference_t<T>* operator->() & noexcept {
        assert(this->is_some());
        return rstd::addressof(_get());
    }

    [[nodiscard]]
    constexpr explicit operator bool() const noexcept {
        return this->is_some();
    }
};

template<typename T>
struct option_adapter;

} // namespace detail
} // namespace rstd::option

namespace rstd
{
export using option::Option;
export using option::Some;
export using option::None;

template<typename T, typename Self>
    requires meta::same_as<T, clone::Clone> &&
             meta::is_specialization_of_v<Self, rstd::option::Option> &&
             Impled<typename option::detail::option_traits<Self>::union_value_t, clone::Clone>
struct Impl<T, Self> : ImplBase<Self> {
    using uv_t = typename option::detail::option_traits<Self>::union_value_t;
    using v_t  = typename option::detail::option_traits<Self>::value_type;
    auto clone() const -> Self {
        auto& o = this->self();
        if (o.is_some()) {
            return Some(Impl<clone::Clone, uv_t> { o._ptr() }.clone());
        }
        return option::Unknown();
    }

    void clone_from(Self& source) {
        auto& self = this->self();
        if (source.is_some()) {
            if constexpr (meta::is_reference_v<v_t>) {
                self._assign_val(source._get());
            } else {
                self._assign_val(Impl<clone::Clone, uv_t> { source._ptr() }.clone());
            }
        } else {
            self._assign_none();
        }
    }
};
} // namespace rstd

namespace rstd::option
{

template<typename T>
class Option : public detail::option_base<T>, public detail::option_adapter<T> {
    template<typename>
    friend struct detail::option_base;
    template<typename>
    friend struct detail::option_adapter;
    template<typename>
    friend struct detail::option_adapter_l1;

    template<typename, typename T_>
    friend constexpr auto rstd::option::Some(T_&& val);
    template<typename, typename T_>
    friend constexpr auto rstd::option::None(T_&& val);

    using traits        = detail::option_base<T>::traits;
    using union_value_t = detail::option_store<T>::union_value_t;

public:
    USE_TRAIT(Option)

    using value_type = T;

    constexpr Option() noexcept = default;
    constexpr Option(Unknown) noexcept: Option() {}

    constexpr Option(const Option&)
        requires meta::is_trivially_copy_constructible_v<union_value_t>
    = default;
    constexpr Option(Option&&)
        requires meta::is_trivially_move_constructible_v<union_value_t>
    = default;

    constexpr Option(Option&& o) noexcept(meta::is_nothrow_move_constructible_v<union_value_t>)
        requires meta::custom_move_constructible<union_value_t>
    {
        if (o.is_some()) {
            this->_construct_val(rstd::move(o)._get());
        } else {
            this->_assign_none();
        }
    }

    ~Option() = default;

    constexpr ~Option()
        requires(! meta::is_trivially_destructible_v<T>)
    {
        if (this->is_some()) {
            rstd::destroy_at(this->_ptr());
        }
    }

    constexpr Option& operator=(const Option&)
        requires meta::is_trivially_copy_assignable_v<union_value_t>
    = default;
    constexpr Option& operator=(Option&&)
        requires meta::is_trivially_move_assignable_v<union_value_t>
    = default;

    constexpr Option&
    operator=(Option&& v) noexcept(meta::is_nothrow_move_assignable_v<union_value_t>)
        requires meta::custom_move_assignable<union_value_t>
    {
        if (v.is_some()) {
            this->_assign_val(rstd::move(v._get()));
        } else {
            this->_assign_none();
        }
        return *this;
    }

private:
    constexpr Option(T&& val) noexcept(meta::nothrow_constructible<T, T>) {
        this->_construct_val(rstd::forward<T>(val));
    }

    constexpr Option(meta::remove_reference_t<T>* ptr) noexcept
        requires meta::is_reference_v<T>
    {
        this->_construct_val(*ptr);
    }
};

} // namespace rstd::option

namespace rstd::option
{

namespace detail
{

template<typename T>
struct option_adapter : option_adapter_l1<T> {
    template<typename E>
    auto ok_or(E err) -> result::Result<T, E> {
        auto& self = static_cast<Option<T>&>(*this);
        if (self.is_some()) {
            return result::Ok(self._get_move());
        } else {
            return result::Err(err);
        }
    }

    template<typename F, typename E = meta::invoke_result_t<F>>
    auto ok_or_else(F&& err) -> result::Result<T, E> {
        auto& self = static_cast<Option<T>&>(*this);
        if (self.is_some()) {
            return result::Ok(self._get_move());
        } else {
            return result::Err(rstd::move(err)());
        }
    }
};

template<typename T>
struct option_adapter_l1<Option<T>> {
    constexpr auto flatten() -> Option<T> {
        auto&& self = static_cast<Option<Option<T>>&&>(*this);
        if (self.is_some()) {
            return self._get_move();
        } else {
            return None();
        }
    }
};

} // namespace detail

} // namespace rstd::option

namespace rstd
{

template<typename U, typename A>
    requires meta::equalable<U, A>
struct Impl<cmp::PartialEq<option::Option<U>>, Option<A>> : ImplBase<Option<A>> {
    auto eq(const Option<U>& other) const noexcept -> bool {
        auto& self = this->self();
        if (self.is_some())
            return other.is_some() && (self._get() == other._get());
        else
            return ! other.is_some();
    }
};

} // namespace rstd
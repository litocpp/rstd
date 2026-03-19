module;
#include <rstd/macro.hpp>
export module rstd.core:option;
export import :clone;
export import :panicking;
export import :slice;
export import :ops.function;
export import :assert;
export import :forward;
export import :cmp;

namespace rstd::option
{

export template<typename T>
class Option;

export struct Unknown {
    template<typename T>
    constexpr operator Option<T>() {
        return {};
    }
};

export template<typename U = void, typename T>
[[gnu::always_inline]]
inline constexpr auto Some(T&& val) {
    if constexpr (mtp::same_as<U, void>) {
        using val_t = mtp::rm_ref<T>;
        if constexpr (mtp::triv_copy<val_t> && (! mtp::is_ref<val_t>)) {
            return Option<val_t>(rstd::move(val));
        } else {
            // use T here, as lvalue is Option<T&>
            // only move is Option<T>
            return Option<T>(rstd::forward<T>(val));
        }
    } else {
        return Option<U>(rstd::forward<T>(val));
    }
}

export template<typename U = void, typename T = Unknown>
[[gnu::always_inline]]
inline constexpr auto None(T&& = {}) {
    if constexpr (mtp::same_as<U, void>) {
        if constexpr (mtp::same_as<Unknown, T>) {
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
    using value_type  = T;
    using ret_value_t = T;
    using union_value_t =
        mtp::cond<mtp::is_ref<T>, mtp::add_ptr<mtp::rm_ref<T>>, T>;
};

template<typename T>
struct option_traits<Option<T>&&> : option_traits<Option<T>> {
    using ret_value_t = mtp::add_ref_rv<T>;
};

template<typename T>
struct option_traits<Option<T>&> : option_traits<Option<T>> {
    using ret_value_t = mtp::add_ref<T>;
};

template<typename T>
struct option_traits<const Option<T>&> : option_traits<Option<T>> {
    using ret_value_t = mtp::add_ref<mtp::add_const<T>>;
};

template<typename T>
struct option_traits<const Option<T>&&> : option_traits<Option<T>> {
    using ret_value_t = mtp::add_ref_rv<mtp::add_const<T>>;
};

export template<typename T>
struct option_store {
    constexpr auto is_some() const noexcept -> bool { return m_has_val; }

protected:
    using union_value_t = T;
    constexpr auto _ptr() const noexcept {
        return rstd::launder(reinterpret_cast<const T*>(m_storage));
    }
    constexpr auto _ptr() noexcept { return rstd::launder(reinterpret_cast<T*>(m_storage)); }
    template<typename V>
    constexpr void _construct_val(V&& val) {
        rstd::construct_at(_ptr(), rstd::forward<V>(val));
        m_has_val = true;
    }
    template<typename V>
    constexpr void _assign_val(V&& val) {
        auto ptr = _ptr();
        if (is_some()) {
            if constexpr (mtp::noex_init<T, V>) {
                rstd::destroy_at(ptr);
                rstd::construct_at(ptr, rstd::forward<V>(val));
            } else if constexpr (mtp::noex_move<T>) {
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
        return rstd::launder(reinterpret_cast<const union_const_value_t*>(m_storage));
    }
    constexpr auto _ptr() noexcept {
        return rstd::launder(reinterpret_cast<union_value_t*>(m_storage));
    }
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
    [[gnu::always_inline]]
    inline static constexpr auto* _ptr_wrapper(U&& self) noexcept {
        if constexpr (mtp::is_ref<T>) {
            return *(self._ptr());
        } else {
            return self._ptr();
        }
    }

    template<typename U>
    [[gnu::always_inline]]
    inline static constexpr auto&& _get(U&& self) noexcept {
        auto* ptr = _ptr_wrapper(rstd::forward<U>(self));

        using value_t = mtp::rm_ref<decltype(*ptr)>;
        if constexpr (mtp::is_ref_lv<U>)
            return static_cast<mtp::add_ref<value_t>>(*ptr);
        else
            return static_cast<mtp::add_ref_rv<value_t>>(*ptr);
    }

    constexpr auto&& _get_move() noexcept { return _get(static_cast<Option<T>&&>(*this)); }

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

    constexpr auto as_ref() const -> Option<mtp::add_ref<mtp::add_const<T>>> {
        if (this->is_some()) {
            return option::Some(_get(*this));
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

    template<typename F, typename U = mtp::invoke_result_t<F, T>>
    // requires ImpledT<FnOnce<F, U(T)>>
    constexpr auto map(F&& f) -> Option<U> {
        if (this->is_some()) {
            return option::Some(rstd::forward<F>(f)(_get_move()));
        }
        return option::None<U>();
    }

    template<typename F, typename U = mtp::invoke_result_t<F, T>>
        requires mtp::spec_of<U, Option>
    constexpr auto and_then(F&& f) -> U {
        if (this->is_some()) {
            return rstd::forward<F>(f)(_get_move());
        }
        return option::None();
    }

    constexpr auto take() -> Option<T> { return rstd::exchange(_cast(), option::None()); }

    [[nodiscard]]
    constexpr auto& operator*() const noexcept {
        assert(this->is_some());
        return _get(*this);
    }

    [[nodiscard]]
    constexpr auto& operator*() noexcept {
        assert(this->is_some());
        return _get(*this);
    }

    [[nodiscard]]
    constexpr auto* operator->() const noexcept {
        assert(this->is_some());
        return _ptr_wrapper(*this);
    }

    [[nodiscard]]
    constexpr auto* operator->() noexcept {
        assert(this->is_some());
        return _ptr_wrapper(*this);
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
    requires mtp::same_as<T, clone::Clone> && mtp::spec_of<Self, rstd::option::Option> &&
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
            if constexpr (mtp::is_ref<v_t>) {
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
    using base_t        = detail::option_base<T>;

public:
    USE_TRAIT(Option)

    using value_type = T;

    constexpr Option() noexcept = default;

    constexpr Option(const Option&) noexcept
        requires mtp::triv_copy<union_value_t>
    = default;
    constexpr Option(Option&&) noexcept
        requires mtp::triv_move<union_value_t>
    = default;

    constexpr Option(Option&& o) noexcept(mtp::noex_move<union_value_t>)
        requires mtp::user_move<union_value_t>
    {
        if (o.is_some()) {
            this->_construct_val(base_t::_get(rstd::move(o)));
        } else {
            this->_assign_none();
        }
    }

    ~Option() = default;

    constexpr ~Option()
        requires(! mtp::triv_drop<T>)
    {
        if (this->is_some()) {
            rstd::destroy_at(this->_ptr());
        }
    }

    constexpr Option& operator=(const Option&)
        requires mtp::triv_assign_copy<union_value_t>
    = default;
    constexpr Option& operator=(Option&&)
        requires mtp::triv_assign_move<union_value_t>
    = default;

    constexpr Option& operator=(Option&& v) noexcept(mtp::noex_move<union_value_t>)
        requires mtp::user_move<union_value_t>
    {
        if (v.is_some()) {
            this->_assign_val(base_t::_get(rstd::move(v)));
        } else {
            this->_assign_none();
        }
        return *this;
    }

private:
    template<typename U>
        requires mtp::init<T, U>
    explicit constexpr Option(U&& val) noexcept(mtp::noex_init<T, U>) {
        this->_construct_val(rstd::forward<U>(val));
    }

    explicit constexpr Option(mtp::rm_ref<T>* ptr) noexcept
        requires mtp::is_ref<T>
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

    template<typename F, typename E = mtp::invoke_result_t<F>>
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
    requires mtp::equalable<U, A>
struct Impl<cmp::PartialEq<option::Option<U>>, Option<A>> : ImplBase<Option<A>> {
    auto eq(const Option<U>& other) const noexcept -> bool {
        auto& self = this->self();
        if (self.is_some())
            return other.is_some() && (*self == *other);
        else
            return ! other.is_some();
    }
};

} // namespace rstd

export module rstd.core:ptr.dyn;
export import :ptr.metadata;
export import :core;
export import :marker;

namespace rstd
{

namespace ptr_
{
export template<typename A>
struct dyn;

template<typename T>
struct VTable {
    static_assert(! meta::is_const_v<T>);
    using trait_api_t = T::template Api<dyn_tag>;

    template<template<class...> typename Tuple>
    using api_tuple_t =
        decltype(meta::to_dyn(meta::TraitApiHelper<T, trait_api_t>::template make<Tuple>()));

    using apis_t   = api_tuple_t<cppstd::tuple>;
    using delete_t = void (*)(voidp);

    delete_t deleter;
    apis_t   apis;
    usize    size;
    usize    align;
};

template<typename T, typename U>
struct VTableStaticStorage {
    using vtable_t = VTable<T>;
    using impl_t   = Impl<T, U>;
    using ApiHelper =
        meta::TraitApiHelper<T, meta::conditional_t<meta::is_direct_trait<T>, U, impl_t>>;
    using apis_t = vtable_t::apis_t;

    template<usize I, typename Fn>
    struct Wrap {
        static_assert(false);
    };

    template<usize I, typename Ret, bool Ne, typename... Args>
    struct Wrap<I, Ret (*)(voidp, Args...) noexcept(Ne)> {
        static auto func(voidp p, Args... args) noexcept(Ne) {
            constexpr const auto api { ApiHelper::template get<I>() };
            if constexpr (meta::is_direct_trait<T>) {
                auto self { static_cast<U*>(p) };
                return (self->*api)(rstd::forward<Args>(args)...);
            } else {
                impl_t self { static_cast<U*>(p) };
                return (self.*api)(rstd::forward<Args>(args)...);
            }
        };
    };

    template<usize I>
    consteval static auto convert() {
        // get api from Impl
        using FT = meta::func_traits<meta::remove_cv_t<decltype(ApiHelper::template get<I>())>>;
        if constexpr (FT::is_member) {
            return &Wrap<I, typename FT::to_dyn>::func;
        } else {
            return ApiHelper::template get<I>();
        }
    }

    template<usize... Is>
    consteval static auto convert_all(cppstd::index_sequence<Is...>) {
        return apis_t { (convert<Is>())... };
    }

    static constexpr const VTable<T> vtable {
        .deleter =
            [](voidp p) {
                // static_cast<U*>(p)->~U();
                delete static_cast<U*>(p);
            },
        .apis  = convert_all(cppstd::make_index_sequence<cppstd::tuple_size_v<apis_t>> {}),
        .size  = sizeof(U),
        .align = alignof(U),
    };
};

template<typename T>
struct dyn_delegate : public meta::remove_cv_t<T>::template Api<dyn_tag> {
    friend struct meta::DynHelper;

    template<typename>
    friend struct dyn;

    using trait_t  = meta::remove_cv_t<T>;
    using vtable_t = VTable<trait_t>;
    using ptr_t    = meta::add_pointer_t<meta::follow_const_t<T, void>>;

    ptr_t           p;
    vtable_t const* vtable;

    template<typename U>
    static auto from_raw_ptr(U* p) noexcept -> dyn_delegate {
        return { .p = static_cast<ptr_t>(p),
                 .vtable =
                     rstd::addressof(VTableStaticStorage<trait_t, meta::remove_cv_t<U>>::vtable) };
    }

    auto operator==(rstd::nullptr_t) const noexcept -> bool { return p == nullptr; }
};

template<typename A>
struct dyn_ptr_base {
    using value_type              = A;
    using trait_t                 = meta::remove_const_t<A>;
    using delegate_t              = meta::follow_const_t<A, dyn_delegate<A>>;
    static constexpr bool Mutable = (! meta::is_const_v<A>);

    friend struct dyn_ptr_base<const trait_t>;

private:
    dyn_delegate<A> d;

public:
    constexpr dyn_ptr_base(dyn_delegate<A> d) noexcept: d(d) {}
    constexpr dyn_ptr_base(const dyn_ptr_base&)            = default;
    constexpr dyn_ptr_base(dyn_ptr_base&&)                 = default;
    constexpr dyn_ptr_base& operator=(const dyn_ptr_base&) = default;
    constexpr dyn_ptr_base& operator=(dyn_ptr_base&&)      = default;

    constexpr dyn_ptr_base(const dyn_ptr_base<trait_t>& v)
        requires(! Mutable)
        : d({ .p = v.d.p, .vtable = v.d.vtable }) {}

    constexpr auto operator->() noexcept -> delegate_t* { return rstd::addressof(d); }
    constexpr auto operator*() noexcept -> delegate_t& { return d; }

    constexpr auto operator==(const dyn_ptr_base& o) const noexcept -> bool { return d.p == o.d.p; }
    constexpr auto operator==(rstd::nullptr_t) const noexcept -> bool { return d == nullptr; }

    constexpr auto as_ptr() const noexcept -> ptr<dyn<trait_t>>
        requires Mutable
    {
        return ptr<dyn<trait_t>> { *this };
    }

    constexpr auto as_ref() const noexcept -> ref<dyn<trait_t>> {
        return ref<dyn<trait_t>> { *this };
    }

    constexpr auto as_mut_ref() const noexcept -> mut_ref<dyn<trait_t>>
        requires Mutable
    {
        return mut_ref<dyn<trait_t>> { *this };
    }

    constexpr void reset() noexcept { d.p = nullptr; }

    constexpr auto* as_raw_ptr() const noexcept { return d.p; }

    constexpr auto* metadata() const noexcept { return d.vtable; }
};

} // namespace ptr_

export using ptr_::dyn;

template<typename A>
struct ref<dyn<A>> : ptr_::dyn_ptr_base<A const> {
    using delegate_t = ptr_::dyn_delegate<A const>;
    static auto from_raw_parts(delegate_t::ptr_t p, delegate_t::vtable_t const* v) -> ref {
        return { { { .p = p, .vtable = v } } };
    }
};
template<typename A>
struct ptr<dyn<A>> : ptr_::dyn_ptr_base<A const> {
    using delegate_t = ptr_::dyn_delegate<A const>;
    static auto from_raw_parts(delegate_t::ptr_t p, delegate_t::vtable_t const* v) -> ptr {
        return { { { .p = p, .vtable = v } } };
    }
};

template<typename A>
struct mut_ref<dyn<A>> : ptr_::dyn_ptr_base<A> {
    using delegate_t = ptr_::dyn_delegate<A>;
    static auto from_raw_parts(delegate_t::ptr_t p, delegate_t::vtable_t const* v) -> mut_ref {
        return { { { .p = p, .vtable = v } } };
    }
};
template<typename A>
struct mut_ptr<dyn<A>> : ptr_::dyn_ptr_base<A> {
    using delegate_t = ptr_::dyn_delegate<A>;
    static auto from_raw_parts(delegate_t::ptr_t p, delegate_t::vtable_t const* v) -> mut_ptr {
        return { { { .p = p, .vtable = v } } };
    }
};

template<meta::same_as<ptr_::Pointee> T, typename A>
struct Impl<T, dyn<A>> {
    using Metadata = meta::add_pointer_t<typename ptr_::dyn_delegate<A>::vtable_t>;
};

template<typename A>
struct Impl<Sized, dyn<A>> {
    ~Impl() = delete;
};

namespace ptr_
{
template<typename A>
struct dyn {
    ~dyn() = delete;

    template<typename T>
    static constexpr auto from_ptr(T* in) noexcept {
        using ptr_t = meta::conditional_t<meta::is_const_v<T>, ptr<dyn>, mut_ptr<dyn>>;
        return ptr_t { { { ptr_t::delegate_t::from_raw_ptr(in) } } };
    }

    template<typename T>
    static constexpr auto from_ref(T& in) noexcept {
        using ref_t = meta::conditional_t<meta::is_const_v<T>, ref<dyn>, mut_ref<dyn>>;
        return ref_t { { { ref_t::delegate_t::from_raw_ptr(rstd::addressof(in)) } } };
    }
};
} // namespace ptr_

namespace meta
{
export template<typename T>
struct dyn_traits {
    static_assert(false);
};

template<typename T>
struct dyn_traits<dyn<T>> {
    template<typename A>
    static constexpr bool Impled = rstd::Impled<A, T>;
};
} // namespace meta

}; // namespace rstd
export module rstd.core:ptr.dyn;
import :num.types;
import :type_id;
export import :ptr.metadata;
export import :core;
export import :marker;

namespace mtp = rstd::mtp;
using rstd::dyn_tag;
using rstd::Impl;
using namespace rstd::prelude;

namespace rstd::ptr_
{
/// A dynamically-typed wrapper enabling trait-object dispatch, analogous to Rust's `dyn Trait`.
/// \tparam A The trait type.
export template<typename A>
struct dyn;

/// Delegate that holds the vtable and data pointer for dynamic dispatch.
/// \tparam T The trait type.
export template<typename T>
struct dyn_delegate;
} // namespace rstd::ptr_

using rstd::ptr_::dyn;
using rstd::ptr_::dyn_delegate;

template<typename T>
struct VTable;

template<typename T, typename U>
struct VTableStaticStorage;

template<typename U>
void dyn_drop(voidp p) {
    static_cast<U*>(p)->~U();
}

template<typename Traits>
struct SuperVTables;

template<>
struct SuperVTables<rstd::TraitList<>> {
    template<typename Super>
    constexpr auto get() const noexcept -> VTable<Super> const* {
        static_assert(mtp::dependent_false<Super>, "trait is not a direct supertrait");
    }
};

template<typename... Traits>
struct SuperVTables<rstd::TraitList<Traits...>> {
    rstd::tuple<VTable<Traits> const*...> values;

    template<typename Super>
    constexpr auto get() const noexcept -> VTable<Super> const* {
        return rstd::get<VTable<Super> const*>(values);
    }
};

template<typename T>
struct VTable {
    static_assert(! mtp::is_const<T>);
    using trait_api_t = T::template Api<dyn_tag>;

    template<template<class...> typename Tuple>
    using api_tuple_t =
        decltype(mtp::to_dyn(mtp::TraitApiHelper<T, trait_api_t>::template make<Tuple>()));

    using apis_t          = api_tuple_t<rstd::tuple>;
    using drop_t          = void (*)(voidp);
    using super_vtables_t = SuperVTables<mtp::trait_super_traits_t<T>>;

    drop_t            drop;
    apis_t            apis;
    super_vtables_t   super_vtables;
    rstd::any::TypeId concrete_type_id;
    usize             size;
    usize             align;
};

template<typename T, typename U>
struct VTableStaticStorage {
    using vtable_t  = VTable<T>;
    using source    = mtp::trait_impl_source<T, U>;
    using impl_t    = Impl<T, U>;
    using ApiHelper = mtp::TraitApiHelper<T, typename source::api_owner>;
    using apis_t    = vtable_t::apis_t;

    template<rstd::size_t I, typename Ret, bool Ne, typename... Args>
    static auto thunk(voidp p, Args... args) noexcept(Ne) -> Ret {
        constexpr const auto api { ApiHelper::template get<I>() };
        if constexpr (source::kind == mtp::trait_impl_kind::Direct ||
                      source::kind == mtp::trait_impl_kind::InClass) {
            auto self { static_cast<U*>(p) };
            return (self->*api)(rstd::forward<Args>(args)...);
        } else {
            impl_t self { static_cast<U*>(p) };
            return (self.*api)(rstd::forward<Args>(args)...);
        }
    }

    template<rstd::size_t I, typename Ret, bool Ne, typename... Args>
    consteval static auto make_thunk(Ret (*)(voidp, Args...) noexcept(Ne)) {
        return &thunk<I, Ret, Ne, Args...>;
    }

    template<rstd::size_t I>
    consteval static auto convert() {
        // get api from Impl
        using FT = mtp::func_traits<mtp::rm_cv<decltype(ApiHelper::template get<I>())>>;
        if constexpr (FT::is_member) {
            using dyn_fn_t = typename FT::to_dyn;
            return make_thunk<I>(static_cast<dyn_fn_t>(nullptr));
        } else {
            return ApiHelper::template get<I>();
        }
    }

    template<rstd::size_t... Is>
    consteval static auto convert_all(mtp::index_sequence<Is...>) {
        return apis_t { (convert<Is>())... };
    }

    template<typename... Supers>
    consteval static auto make_super_vtables(rstd::TraitList<Supers...>) {
        using result_t = SuperVTables<rstd::TraitList<Supers...>>;
        if constexpr (sizeof...(Supers) == 0) {
            return result_t {};
        } else {
            return result_t { .values = {
                                  rstd::addressof(VTableStaticStorage<Supers, U>::vtable)... } };
        }
    }

    static constexpr const VTable<T> vtable {
        .drop             = &dyn_drop<U>,
        .apis             = convert_all(mtp::make_index_sequence<mtp::tuple_size<apis_t>> {}),
        .super_vtables    = make_super_vtables(mtp::trait_super_traits_t<T> {}),
        .concrete_type_id = rstd::any::TypeId::of<U>(),
        .size             = usize(sizeof(U)),
        .align            = usize(alignof(U)),
    };
};

template<typename T>
struct rstd::ptr_::dyn_delegate : public mtp::rm_cv<T>::template Api<dyn_tag> {
    friend struct mtp::DynHelper;

    template<typename>
    friend struct dyn;

    using trait_t  = mtp::rm_cv<T>;
    using vtable_t = VTable<trait_t>;
    using ptr_t    = voidp;

    ptr_t           p;
    vtable_t const* vtable;

    template<typename U, typename Storage>
    static auto from_storage_ptr(Storage* p) noexcept -> dyn_delegate {
        using class_t = mtp::rm_cv<U>;
        using source  = mtp::trait_impl_source<trait_t, class_t>;
        if constexpr (! source::value) {
            static_assert(mtp::check_trait_or_diagnose<trait_t, class_t>());
        } else {
            return { .p      = const_cast<voidp>(static_cast<const void*>(p)),
                     .vtable = rstd::addressof(VTableStaticStorage<trait_t, class_t>::vtable) };
        }
    }

    template<typename U>
    static auto from_raw_ptr(U* p) noexcept -> dyn_delegate {
        return from_storage_ptr<U>(p);
    }

    auto operator==(std::nullptr_t) const noexcept -> bool { return p == nullptr; }
};

template<typename A>
struct dyn_ptr_base {
    using value_type              = A;
    using trait_t                 = mtp::rm_const<A>;
    using delegate_storage_t      = dyn_delegate<trait_t>;
    using delegate_t              = mtp::follow_const_t<A, delegate_storage_t>;
    using raw_ptr_t               = mtp::add_ptr<mtp::follow_const_t<A, void>>;
    static constexpr bool Mutable = (! mtp::is_const<A>);

    friend struct dyn_ptr_base<const trait_t>;

private:
    delegate_storage_t d;

public:
    constexpr dyn_ptr_base() noexcept: d {} {}
    constexpr dyn_ptr_base(delegate_storage_t d) noexcept: d(d) {}
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
    constexpr auto operator==(std::nullptr_t) const noexcept -> bool { return d == nullptr; }

    constexpr auto as_ptr() const noexcept -> rstd::ptr<dyn<trait_t>>
        requires Mutable
    {
        return rstd::ptr<dyn<trait_t>> { *this };
    }

    constexpr auto as_ref() const noexcept -> rstd::ref<dyn<trait_t>> {
        return rstd::ref<dyn<trait_t>> { *this };
    }

    constexpr auto as_mut_ref() const noexcept -> rstd::mut_ref<dyn<trait_t>>
        requires Mutable
    {
        return rstd::mut_ref<dyn<trait_t>> { *this };
    }

    constexpr void reset() noexcept {
        d.p      = nullptr;
        d.vtable = nullptr;
    }

    constexpr auto as_raw_ptr() const noexcept -> raw_ptr_t { return d.p; }

    constexpr auto metadata() const noexcept -> delegate_t::vtable_t const* { return d.vtable; }

    constexpr auto concrete_type_id() const noexcept -> rstd::any::TypeId {
        return d.vtable->concrete_type_id;
    }

    template<typename Super, rstd::size_t I, typename... Args>
    constexpr decltype(auto) call_super(Args&&... args) const {
        auto super = d.vtable->super_vtables.template get<Super>();
        return rstd::get<I>(super->apis)(d.p, rstd::forward<Args>(args)...);
    }
};

namespace rstd
{

/// Re-export of `ptr_::dyn` for trait-object pointers.
export using ptr_::dyn;

template<typename A>
struct ref<dyn<A>> : dyn_ptr_base<A const> {
    using delegate_t = dyn_delegate<A>;
    using raw_ptr_t  = typename dyn_ptr_base<A const>::raw_ptr_t;

    static auto from_raw_parts(raw_ptr_t p [[clang::lifetimebound]], delegate_t::vtable_t const* v)
        -> ref {
        return { { { .p = const_cast<voidp>(p), .vtable = v } } };
    }

    constexpr auto deref() const noexcept -> ref<dyn<A>> { return *this; }
};
template<typename A>
struct ptr<dyn<A>> : dyn_ptr_base<A const> {
    using delegate_t = dyn_delegate<A>;
    using raw_ptr_t  = typename dyn_ptr_base<A const>::raw_ptr_t;
    static auto from_raw_parts(raw_ptr_t p [[clang::lifetimebound]], delegate_t::vtable_t const* v)
        -> ptr {
        return { { { .p = const_cast<voidp>(p), .vtable = v } } };
    }
};

template<typename A>
struct mut_ref<dyn<A>> : dyn_ptr_base<A> {
    using delegate_t = dyn_delegate<A>;

    static auto from_raw_parts(delegate_t::ptr_t           p [[clang::lifetimebound]],
                               delegate_t::vtable_t const* v) -> mut_ref {
        return { { { .p = p, .vtable = v } } };
    }

    constexpr auto deref() const noexcept -> ref<dyn<A>> { return this->as_ref(); }
    constexpr auto deref_mut() noexcept -> mut_ref<dyn<A>> { return *this; }
};
template<typename A>
struct mut_ptr<dyn<A>> : dyn_ptr_base<A> {
    using delegate_t = dyn_delegate<A>;
    static auto from_raw_parts(delegate_t::ptr_t           p [[clang::lifetimebound]],
                               delegate_t::vtable_t const* v) -> mut_ptr {
        return { { { .p = p, .vtable = v } } };
    }
};

template<typename A>
struct Impl<ptr_::Pointee, dyn<A>> {
    using Metadata = mtp::add_ptr<typename dyn_delegate<A>::vtable_t>;
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
    static constexpr auto from_ptr(T* in [[clang::lifetimebound]]) noexcept {
        using ptr_t = mtp::cond<mtp::is_const<T>, ptr<dyn>, mut_ptr<dyn>>;
        return ptr_t { { { ptr_t::delegate_t::from_raw_ptr(in) } } };
    }

    template<typename T>
    static constexpr auto from_ptr(mut_ptr<T> in [[clang::lifetimebound]]) noexcept {
        using ptr_t = mut_ptr<dyn>;
        return ptr_t { { { ptr_t::delegate_t::template from_storage_ptr<T>(in.as_raw_ptr()) } } };
    }

    template<typename T>
    static constexpr auto from_ref(T& in [[clang::lifetimebound]]) noexcept {
        using ref_t = mtp::cond<mtp::is_const<T>, ref<dyn>, mut_ref<dyn>>;
        return ref_t { { { ref_t::delegate_t::from_raw_ptr(rstd::addressof(in)) } } };
    }
};
} // namespace ptr_

namespace mtp
{
/// Traits for extracting information from a `dyn<T>` type.
/// \tparam T The dyn type to inspect.
export template<typename T>
struct dyn_traits {
    static_assert(false);
};

template<typename T>
struct dyn_traits<dyn<T>> {
    template<typename A>
    static constexpr bool Impled = rstd::Impled<A, T>;
};
} // namespace mtp

}; // namespace rstd

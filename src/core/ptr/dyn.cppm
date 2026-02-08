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
class dyn_delegate : public meta::remove_cv_t<T>::template Api<dyn_tag> {
    friend struct detail::DynHelper;

    template<typename>
    friend struct dyn;

    using trait_t     = meta::remove_cv_t<T>;
    using trait_api_t = trait_t::template Api<dyn_tag>;

    template<template<class...> typename Tuple>
    using dyn_vtable_t = decltype(detail::to_dyn(
        detail::TraitApiHelper<trait_t, trait_api_t>::template make<Tuple>()));

public:
    using vtable_t = dyn_vtable_t<cppstd::tuple>;

private:
    using ptr_t = meta::add_pointer_t<meta::follow_const_t<T, void>>;
    const vtable_t* const apis;
    ptr_t                 self;

    template<typename U>
    struct VTable {
        using impl_t    = Impl<trait_t, U>;
        using ApiHelper = detail::TraitApiHelper<trait_t, impl_t>;

        template<usize I, typename Fn>
        struct Wrap {
            static_assert(false);
        };

        template<usize I, typename Ret, bool Ne, typename... Args>
        struct Wrap<I, Ret (*)(voidp, Args...) noexcept(Ne)> {
            static auto func(voidp p, Args... args) noexcept(Ne) {
                impl_t               self { static_cast<const U*>(p) };
                constexpr const auto api { ApiHelper::template get<I>() };
                return cppstd::invoke(api, self, rstd::forward<Args>(args)...);
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
            return vtable_t { (convert<Is>())... };
        }

        constexpr static vtable_t apis { convert_all(
            cppstd::make_index_sequence<cppstd::tuple_size_v<vtable_t>> {}) };
    };

    template<typename U>
    constexpr dyn_delegate(U* p) noexcept
        : apis(&VTable<meta::remove_cv_t<U>>::apis), self(static_cast<ptr_t>(p)) {}

public:
    constexpr dyn_delegate(const dyn_delegate&) noexcept = default;
    constexpr dyn_delegate(dyn_delegate&&) noexcept      = default;

    constexpr dyn_delegate& operator=(const dyn_delegate&) noexcept = default;
    constexpr dyn_delegate& operator=(dyn_delegate&&) noexcept      = default;

    auto operator==(rstd::nullptr_t) const noexcept -> bool { return self == nullptr; }
};

template<typename A, bool Mutable>
struct dyn_ptr_base {
    using value_type = A;
    using delegate_t = meta::conditional_t<Mutable, dyn_delegate<A>, const dyn_delegate<A const>>;
    delegate_t d;

    auto operator->() noexcept { return &d; }
    auto operator==(rstd::nullptr_t) const noexcept -> bool { return d == nullptr; }
};

} // namespace ptr_

export using ptr_::dyn;

template<typename A>
struct ref<dyn<A>> : ptr_::dyn_ptr_base<A, false> {};
template<typename A>
struct ptr<dyn<A>> : ptr_::dyn_ptr_base<A, false> {};

template<typename A>
struct mut_ref<dyn<A>> : ptr_::dyn_ptr_base<A, true> {};
template<typename A>
struct mut_ptr<dyn<A>> : ptr_::dyn_ptr_base<A, true> {};

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
    static auto from_ptr(T* in) noexcept {
        using ptr_t = meta::conditional_t<meta::is_const_v<T>, ptr<dyn>, mut_ptr<dyn>>;
        return ptr_t { { .d { in } } };
    }

    template<typename T>
    static auto from_ref(T& in) noexcept {
        using ref_t = meta::conditional_t<meta::is_const_v<T>, ref<dyn>, mut_ref<dyn>>;
        return ref_t { { .d { rstd::addressof(in) } } };
    }
};
} // namespace ptr_

}; // namespace rstd
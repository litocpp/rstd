export module rstd.core:trait;
export import :basic;
export import :meta;

namespace rstd
{

/// Helper for collect trait api
export template<auto... Api>
struct TraitFuncs {};

/// Impl for trait
export template<typename T, typename A>
struct Impl;

namespace mtp
{
template<typename T>
struct TraitApiTraits;
}

namespace mtp
{
export template<typename T>
concept is_trait_api = requires() {
    typename T::Trait;
    typename mtp::TraitApiTraits<T>::type;
};

export template<typename T>
concept is_direct_trait = T::direct;

export template<typename T>
concept has_trait_api = requires { T::Api; };

} // namespace mtp

namespace ptr_
{
export template<typename T>
struct dyn_delegate;
}

enum class TraitDefaultPolicy
{
    InClass,
    Normal
};

export template<typename T, TraitDefaultPolicy P = TraitDefaultPolicy::Normal>
struct default_tag {};

struct in_class_tag {};

struct dyn_tag {};

namespace mtp
{

struct ImplHelper;

template<typename T>
struct TraitFuncsHelper;

template<auto... Api>
struct TraitFuncsHelper<TraitFuncs<Api...>> {
    template<template<class...> typename T>
    consteval static auto make() {
        return T { Api... };
    }

    template<usize I>
    consteval static auto get() {
        return mtp::get_auto<I, Api...>();
    }

    consteval static auto size() { return sizeof...(Api); }

    template<usize I>
    using type_at = decltype(get<I>());
};

template<typename T>
struct ImplWithPtr {
    usizeptr ptr_;

    friend struct ImplHelper;

    template<typename P>
    constexpr ImplWithPtr(P* p) noexcept: ptr_(rstd::bit_cast<usizeptr>(p)) {}

protected:
    constexpr auto self() noexcept -> T& { return *rstd::bit_cast<T*>(ptr_); }
    constexpr auto self() const noexcept -> T const& { return *rstd::bit_cast<T const*>(ptr_); }
};

template<template<typename, typename> class T, typename A, typename B>
struct TraitApiTraits<T<A, B>> {
    using type          = A;
    using delegate_type = B;
};

template<typename Trait, typename A, typename Delegate = void>
using TraitApi = typename Trait::template Api<A, Delegate>;

template<typename Trait, typename A>
using TraitFuncs = typename Trait::template Funcs<A>;

template<typename Trait, typename A>
using TraitApiHelper = TraitFuncsHelper<TraitFuncs<Trait, A>>;

template<typename Trait, typename A, typename B, usize Index = 0>
consteval bool check_apis() {
    using ApiHelperA = TraitApiHelper<Trait, A>;
    using ApiHelperB = TraitApiHelper<Trait, B>;

    if constexpr (Index == 0) {
        static_assert(ApiHelperA::size() == ApiHelperB::size(), "Please implement all Trait api");
    }

    if constexpr (Index < ApiHelperA::size()) {
        using T1 = ApiHelperA::template type_at<Index>;
        using T2 = ApiHelperB::template type_at<Index>;

        using F1 = mtp::func_traits<T1>;
        using F2 = mtp::func_traits<T2>;
        using P1 = typename F1::primary;
        using P2 = typename F2::primary;

        static_assert(F1::is_member == F2::is_member);

        if constexpr (F1::is_member) {
            // Is member func
            static_assert(mtp::is_lvalue_reference_v<P1> == mtp::is_lvalue_reference_v<P2> &&
                              mtp::is_rvalue_reference_v<P1> == mtp::is_rvalue_reference_v<P2> &&
                              mtp::is_const_v<P1> == mtp::is_const_v<P2> &&
                              mtp::same_as<typename F1::to_dyn, typename F2::to_dyn>,
                          "Trait api not satisfy");
        } else {
            // not member func, full compare
            static_assert(mtp::same_as<T1, T2>, "Trait api not satisfy");
        }
        return check_apis<Trait, A, B, Index + 1>();
    }
    return true;
}

template<typename... Api, template<class...> typename Tuple>
consteval auto to_dyn(Tuple<Api...>) {
    return Tuple { (typename mtp::func_traits<Api>::to_dyn)(nullptr)... };
}
struct DynHelper {
    template<typename TDyn>
    static auto get_self(TDyn* t) noexcept {
        return t->p;
    }

    template<typename TDyn>
    static const auto* get_apis(TDyn const* t) noexcept {
        return rstd::addressof(t->vtable->apis);
    }
};

struct ImplHelper {
    template<typename T>
    static auto& get_self(T* t) noexcept {
        return t->self();
    }
};

export template<typename T, typename A, typename ToCheck>
consteval bool check_trait_apis() {
    static_assert(mtp::has_trait_api<T>);
    return check_apis<T, TraitApi<T, A>, ToCheck>();
}

export template<typename T, typename A>
consteval bool check_trait() {
    if constexpr (mtp::same_as<A, dyn_tag>) {
        return true;
    } else if constexpr (mtp::is_direct_trait<T>) {
        if constexpr (mtp::has_trait_api<T>) {
            // check apis on A
            return check_trait_apis<T, A, A>();
        } else {
            return true;
        }
    } else if constexpr (mtp::destructible<Impl<T, A>>) {
        if constexpr (mtp::has_trait_api<T>) {
            // check apis on Impl<T,A>
            return check_trait_apis<T, A, Impl<T, A>>();
        } else {
            return true;
        }
    }
    return false;
}

} // namespace mtp



export template<typename A>
struct ImplBase : mtp::ImplWithPtr<A> {};

// This is used as base class for user class
// not the Impl class
template<typename A>
struct ImplBase<default_tag<A, TraitDefaultPolicy::InClass>> {
    template<typename, typename>
    friend struct Impl;

private:
    auto self() -> A& { return *static_cast<A*>(this); }
    auto self() const -> A const& { return *static_cast<A const*>(this); }
};

template<typename A>
struct ImplBase<default_tag<A, TraitDefaultPolicy::Normal>> : ImplBase<A> {};

/// check if has Impl
export template<typename A, typename... T>
concept Impled = (mtp::check_trait<T, mtp::remove_cvref_t<A>>() && ...);

export template<typename T, typename A, TraitDefaultPolicy P = TraitDefaultPolicy::Normal>
using ImplDefault = Impl<T, default_tag<A, P>>;

export template<typename T, typename A>
struct ImplInClass : mtp::ImplWithPtr<A>, mtp::remove_cv_t<T>::template Api<A, in_class_tag> {};

/// delegate trait call to impl
export template<usize I, typename TApi, typename... Args>
    requires mtp::is_trait_api<mtp::remove_cv_t<TApi>>
[[gnu::always_inline]]
inline constexpr decltype(auto) trait_call(TApi* self, Args&&... args) {
    using TApi_    = mtp::remove_cv_t<TApi>;
    using Trait    = typename TApi_::Trait;
    using TClass   = typename mtp::TraitApiTraits<TApi_>::type;
    using Delegate = typename mtp::TraitApiTraits<TApi_>::delegate_type;
    using TImpl    = Impl<Trait, TClass>;

    if constexpr (mtp::same_as<TClass, dyn_tag>) {
        // delegate for dyn
        auto dyn = static_cast<mtp::follow_const_t<TApi, ptr_::dyn_delegate<Trait>>*>(self);

        const auto* apis = mtp::DynHelper::get_apis(dyn);
        return rstd::get<I>(*apis)(mtp::DynHelper::get_self(dyn), rstd::forward<Args>(args)...);
    } else if constexpr (mtp::same_as<Delegate, in_class_tag>) {
        // delegate for ImplInClass
        // used for trait impl inherite Api
        constexpr const auto api { mtp::TraitApiHelper<Trait, TClass>::template get<I>() };

        auto impl_in_class =
            static_cast<mtp::follow_const_t<TApi, ImplInClass<Trait, TClass>>*>(self);

        const auto self_ = rstd::addressof(mtp::ImplHelper::get_self(impl_in_class));
        return (self_->*api)(rstd::forward<Args>(args)...);
    } else {
        // delegate for Impl
        // used for class inherite Api
        constexpr const auto api { mtp::TraitApiHelper<Trait, TImpl>::template get<I>() };

        TImpl const self_ { static_cast<mtp::follow_const_t<TApi, TClass>*>(self) };
        return (self_.*api)(rstd::forward<Args>(args)...);
    }
}

/// delegate static trait call to impl
export template<usize I, typename TApi, typename... Args>
    requires mtp::is_trait_api<mtp::remove_cv_t<TApi>>
[[gnu::always_inline]]
inline constexpr decltype(auto) trait_static_call(Args&&... args) {
    using TApi_  = mtp::remove_cv_t<TApi>;
    using Trait  = typename TApi_::Trait;
    using TClass = typename mtp::TraitApiTraits<TApi_>::type;
    using TImpl  = Impl<Trait, TClass>;

    constexpr const auto api { mtp::TraitApiHelper<Trait, TImpl>::template get<I>() };
    return api(rstd::forward<Args>(args)...);
}

/// as a triat
/// only accept lvalue for no stack-use-after-scope
/// requires Impled<T, mtp::remove_cvref_t<A>>, maybe used in impldef which not satisfy
export template<typename T, typename A>
[[gnu::always_inline]]
inline constexpr decltype(auto) as(A& t) noexcept {
    using class_t = mtp::remove_cvref_t<A>;
    static_assert(Impled<class_t, T>);
    if constexpr (mtp::is_direct_trait<T>) {
        return t;
    } else {
        using impl_t = Impl<T, class_t>;
        using ret_t  = mtp::follow_const_t<A, impl_t>;
        return ret_t { rstd::addressof(t) };
    }
}

export template<typename Self, typename... Traits>
struct WithTrait : Traits::template Api<Self>... {
    constexpr bool operator==(const WithTrait) const { return true; }
};

export template<typename Self, typename... Traits>
struct WithTraitDefault : ImplDefault<Traits, Self, TraitDefaultPolicy::InClass>... {
    constexpr bool operator==(const WithTraitDefault) const { return true; }
};

export template<typename Self, typename... Traits>
struct MayWithTrait
    : mtp::conditional_t<Impled<Traits, Self>, typename Traits::template Api<Self>, empty>... {
    constexpr bool operator==(const MayWithTrait) const { return true; }
};

} // namespace rstd
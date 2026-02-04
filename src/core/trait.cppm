export module rstd.core:trait;
export import :basic;
export import :meta;

namespace rstd
{
export template<auto... Api>
struct TraitFuncs {};

namespace ptr_
{
export template<typename T>
class dyn_delegate;
}

namespace detail
{

struct ImplHelper;

template<typename T>
struct TraitFuncsInner;

template<auto... Api>
struct TraitFuncsInner<TraitFuncs<Api...>> {
    template<template<class...> typename T>
    consteval static auto make() {
        return T { Api... };
    }

    template<usize I>
    consteval static auto get() {
        return meta::get_auto<I, Api...>();
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

template<typename T>
struct ApiInner;

template<template<typename, typename> class T, typename A, typename B>
struct ApiInner<T<A, B>> {
    using type          = A;
    using delegate_type = B;
};

template<typename Self, typename TApi, typename TApiB, usize Index = 0>
consteval bool check_apis() {
    if constexpr (Index == 0) {
        static_assert(TApi::size() == TApiB::size(), "Please implement all Trait api");
    }

    if constexpr (Index < TApi::size()) {
        using T1 = TApi::template type_at<Index>;
        using T2 = TApiB::template type_at<Index>;

        using F1 = meta::func_traits<T1>;
        using F2 = meta::func_traits<T2>;
        using P1 = typename F1::primary;
        using P2 = typename F2::primary;

        if constexpr (meta::same_as<Self, meta::remove_cv_t<meta::remove_reference_t<P1>>>) {
            // Is member func
            static_assert(meta::is_lvalue_reference_v<P1> == meta::is_lvalue_reference_v<P2> &&
                              meta::is_rvalue_reference_v<P1> == meta::is_rvalue_reference_v<P2> &&
                              meta::is_const_v<P1> == meta::is_const_v<P2> &&
                              meta::same_as<typename F1::to_dyn, typename F2::to_dyn>,
                          "Trait api not satisfy");
        } else {
            // not member func, full compare
            static_assert(meta::same_as<T1, T2>, "Trait api not satisfy");
        }
        return check_apis<Self, TApi, TApiB, Index + 1>();
    }
    return true;
}

template<typename... Api, template<class...> typename Tuple>
consteval auto to_dyn(Tuple<Api...>) {
    return Tuple { (typename meta::func_traits<Api>::to_dyn)(nullptr)... };
}

template<typename Trait, typename T>
using Funcs = typename Trait::template Funcs<T>;

template<typename Trait, typename T>
using TraitApiHelper = TraitFuncsInner<Funcs<Trait, T>>;

struct DynHelper {
    template<typename TDyn>
    static auto get_self(TDyn* t) noexcept {
        return t->self;
    }

    template<typename TDyn>
    static auto get_apis(TDyn* t) noexcept {
        return t->apis;
    }
};

struct ImplHelper {
    template<typename T>
    static auto& get_self(T* t) noexcept {
        return t->self();
    }
};

} // namespace detail

export template<typename T, typename A>
struct Impl;

enum class TraitDefaultPolicy
{
    InClass,
    Normal
};

export template<typename A>
struct ImplBase : detail::ImplWithPtr<A> {};

export template<typename T, TraitDefaultPolicy P = TraitDefaultPolicy::Normal>
struct default_tag {};

struct in_class_tag {};

struct dyn_tag {};

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

export template<typename A, typename... T>
concept Impled = (meta::destructible<Impl<T, meta::remove_cvref_t<A>>> && ...) ||
                 meta::same_as<meta::remove_cvref_t<A>, dyn_tag>;

export template<typename T>
concept IsTraitApi = requires() {
    typename T::Trait;
    typename detail::ApiInner<T>::type;
};

export template<typename T, typename A, TraitDefaultPolicy P = TraitDefaultPolicy::Normal>
using ImplDefault = Impl<T, default_tag<A, P>>;

export template<typename T, typename A>
struct ImplInClass : detail::ImplWithPtr<A>, meta::remove_cv_t<T>::template Api<A, in_class_tag> {};

export template<usize I, typename TApi, typename... Args>
    requires IsTraitApi<meta::remove_cv_t<TApi>>
[[gnu::always_inline]] inline constexpr decltype(auto) trait_call(TApi* self, Args&&... args) {
    using TApi_    = meta::remove_cv_t<TApi>;
    using Trait    = typename TApi_::Trait;
    using TClass   = typename detail::ApiInner<TApi_>::type;
    using Delegate = typename detail::ApiInner<TApi_>::delegate_type;
    using TImpl    = Impl<Trait, TClass>;

    if constexpr (meta::same_as<TClass, dyn_tag>) {
        // delegate for dyn
        auto dyn = static_cast<meta::follow_const_t<TApi, ptr_::dyn_delegate<Trait>>*>(self);
        return rstd::get<I>(*detail::DynHelper::get_apis(dyn))(detail::DynHelper::get_self(dyn),
                                                               rstd::forward<Args>(args)...);
    } else if constexpr (meta::same_as<Delegate, in_class_tag>) {
        // delegate for ImplInClass
        // used for trait impl inherite Api
        constexpr const auto api { detail::TraitApiHelper<Trait, TClass>::template get<I>() };

        auto impl_in_class =
            static_cast<meta::follow_const_t<TApi, ImplInClass<Trait, TClass>>*>(self);

        auto self_ = rstd::addressof(detail::ImplHelper::get_self(impl_in_class));
        return cppstd::invoke(api, self_, rstd::forward<Args>(args)...);
    } else {
        // delegate for Impl
        // used for class inherite Api
        constexpr const auto api { detail::TraitApiHelper<Trait, TImpl>::template get<I>() };

        TImpl self_ { static_cast<meta::follow_const_t<TApi, TClass>*>(self) };
        return cppstd::invoke(api, self_, rstd::forward<Args>(args)...);
    }
}

export template<usize I, typename TApi, typename... Args>
    requires IsTraitApi<meta::remove_cv_t<TApi>>
[[gnu::always_inline]] inline constexpr decltype(auto) trait_static_call(Args&&... args) {
    using TApi_  = meta::remove_cv_t<TApi>;
    using Trait  = typename TApi_::Trait;
    using TClass = typename detail::ApiInner<TApi_>::type;
    using TImpl  = Impl<Trait, TClass>;

    constexpr const auto api { detail::TraitApiHelper<Trait, TImpl>::template get<I>() };
    return cppstd::invoke(api, rstd::forward<Args>(args)...);
}

///
/// @brief as a triat
// only accept lvalue for no stack-use-after-scope
// requires Impled<T, meta::remove_cvref_t<A>>, maybe used in impldef which not satisfy
export template<typename T, typename A>
[[gnu::always_inline]] inline constexpr auto as(A& t) noexcept {
    using impl_t = Impl<T, meta::remove_cvref_t<A>>;
    using ret_t  = meta::conditional_t<meta::is_const_v<A>, meta::add_const_t<impl_t>, impl_t>;
    return ret_t { rstd::addressof(t) };
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
    : meta::conditional_t<Impled<Traits, Self>, typename Traits::template Api<Self>, Empty>... {
    constexpr bool operator==(const MayWithTrait) const { return true; }
};

} // namespace rstd
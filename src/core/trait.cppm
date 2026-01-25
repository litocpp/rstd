export module rstd.core:trait;
export import :core;
export import :cppstd;
export import :meta;

namespace rstd
{
export template<auto... Api>
struct TraitCollect {};

namespace detail
{

struct ImplHelper;

template<typename T>
struct TraitCollectInner;

template<auto... Api>
struct TraitCollectInner<TraitCollect<Api...>> {
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
using TCollect = typename Trait::template TCollect<T>;

template<typename Trait, typename T>
using TraitApiHelper = TraitCollectInner<TCollect<Trait, T>>;

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

export template<typename T>
class Dyn;

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
        auto dyn = static_cast<meta::follow_const_t<TApi, Dyn<Trait>>*>(self);
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

template<typename T>
class Dyn : public meta::remove_cv_t<T>::template Api<dyn_tag> {
    friend struct detail::DynHelper;

    using trait_t     = meta::remove_cv_t<T>;
    using trait_api_t = trait_t::template Api<dyn_tag>;

    template<template<class...> typename Tuple>
    using dyn_apis_t = decltype(detail::to_dyn(
        detail::TraitApiHelper<trait_t, trait_api_t>::template make<Tuple>()));

    using ptr_t  = meta::conditional_t<meta::is_const_v<T>, const_voidp, voidp>;
    using apis_t = dyn_apis_t<cppstd::tuple>;

    const apis_t* const apis;
    ptr_t               self;

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
            return apis_t { (convert<Is>())... };
        }

        constexpr static apis_t apis { convert_all(
            cppstd::make_index_sequence<cppstd::tuple_size_v<apis_t>> {}) };
    };

    template<typename U>
    constexpr Dyn(U* p) noexcept
        : apis(&VTable<meta::remove_cv_t<U>>::apis), self(static_cast<ptr_t>(p)) {}

public:
    template<typename U>
    constexpr static auto make(U* p) noexcept -> Dyn {
        return { p };
    }
    template<typename U>
    constexpr static auto make(U& p) noexcept -> Dyn {
        return { rstd::addressof(p) };
    }

    constexpr Dyn(const Dyn&) noexcept = default;
    constexpr Dyn(Dyn&&) noexcept      = default;

    constexpr Dyn& operator=(const Dyn&) noexcept = default;
    constexpr Dyn& operator=(Dyn&&) noexcept      = default;
};

export template<typename T, typename A>
constexpr auto make_dyn(A* t) noexcept {
    using dyn_t = meta::conditional_t<meta::is_const_v<A>, Dyn<const T>, Dyn<T>>;
    return dyn_t::make(t);
}

export template<typename T, typename A>
constexpr auto make_dyn(A& t) noexcept {
    using dyn_t = meta::conditional_t<meta::is_const_v<A>, Dyn<const T>, Dyn<T>>;
    return dyn_t::make(t);
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
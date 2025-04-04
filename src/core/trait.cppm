module;
#include <tuple>
#include <functional>
export module rstd.core:trait;
export import :basic;
export import :meta;

namespace rstd
{
export template<auto... Api>
struct TraitCollect {};

namespace detail
{
template<typename... Api>
struct Apis {
    std::tuple<Api...> apis;

    constexpr Apis(Api... api) noexcept: apis(api...) {}

    template<std::size_t I>
    constexpr auto get() const noexcept {
        return std::get<I>(apis);
    }
    template<std::size_t I>
    using type_at = std::tuple_element_t<I, decltype(apis)>;

    using to_tuple_t = std::tuple<Api...>;

    consteval static auto size() noexcept { return std::tuple_size<decltype(apis)> {}; }
};
template<typename T>
struct FromCollect;
template<auto... Api>
struct FromCollect<TraitCollect<Api...>> {
    consteval static auto get() { return Apis { Api... }; }
};

template<typename T>
struct ApiInner;

template<template<typename, typename> class T, typename A, typename B>
struct ApiInner<T<A, B>> {
    using type          = A;
    using delegate_type = B;
};

template<template<typename, typename> class T, typename A, typename B>
struct ApiInner<const T<A, B>> {
    using type          = A;
    using delegate_type = B;
};

template<typename T>
struct FuncTraits;

template<typename Self, typename TApi, typename TApiB, std::size_t Index = 0>
consteval void check_apis() {
    if constexpr (Index == 0) {
        static_assert(TApi::size() == TApiB::size(), "Please implement all Trait api");
    }

    if constexpr (Index < TApi::size()) {
        using T1 = TApi::template type_at<Index>;
        using T2 = TApiB::template type_at<Index>;

        using F1 = FuncTraits<T1>;
        using F2 = FuncTraits<T2>;
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
        check_apis<Self, TApi, TApiB, Index + 1>();
    }
}

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (*)(T, Args...) noexcept(Ne)> {
    static constexpr bool is_member = false;

    using primary = meta::conditional_t<meta::is_pointer_v<T>, void, T>;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename Ret, bool Ne>
struct FuncTraits<Ret (*)(void) noexcept(Ne)> {
    static constexpr bool is_member = false;

    using primary = void;

    using to_dyn = Ret (*)(void) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) & noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) && noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = T&&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) const noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = const T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) const & noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = const T&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) const && noexcept(Ne)> {
    static constexpr bool is_member = true;

    using primary = const T&&;

    using to_dyn = Ret (*)(voidp, Args...) noexcept(Ne);
};

template<typename... Api>
consteval auto to_dyn(Apis<Api...> in) {
    return Apis { (typename FuncTraits<Api>::to_dyn)(nullptr)... };
}

} // namespace detail

export template<typename T, typename A>
struct Impl;

export template<typename A>
struct ImplBase {
    A const* _self;

protected:
    auto self() -> A& { return *const_cast<A*>(_self); }
    auto self() const -> A const& { return *_self; }
};

enum class DefPolicy
{
    InClass,
    Normal
};

export template<typename T, DefPolicy P = DefPolicy::Normal>
struct Def {};

export template<typename T, typename A>
struct ImplDefaultBase;

export template<typename T, typename A>
struct ImplDefaultBase<T, Def<A, DefPolicy::InClass>> {
    template<typename, typename>
    friend struct Impl;

private:
    auto self() -> A& { return *static_cast<A*>(this); }
    auto self() const -> A const& { return *static_cast<A const*>(this); }
};

export template<typename T, typename A>
struct ImplDefaultBase<T, Def<A, DefPolicy::Normal>> : ImplBase<A> {};

struct dyn_tag {};
template<typename T>
struct in_class_tag {
    using type = T;
};

export template<typename A, typename... T>
concept Impled = (std::semiregular<Impl<T, meta::remove_cvref_t<A>>> && ...) ||
                 meta::same_as<meta::remove_cvref_t<A>, dyn_tag>;

export template<typename T>
concept IsTraitApi = requires() {
    typename T::Trait;
    typename detail::ApiInner<T>::type;
};

export template<typename T>
struct Dyn;

export template<typename T, typename A, DefPolicy P = DefPolicy::Normal>
using ImplDefault = Impl<T, Def<A, P>>;

export template<typename T, typename A>
struct ImplInClass;

export template<typename T, typename A, typename Delegate = void>
struct TraitMeta {
    using Api = typename T::template Api<A, Delegate>;
    template<typename F>
    using TCollect = typename T::template TCollect<F>;

    static consteval auto collect() {
        constexpr auto req_apis = detail::FromCollect<TCollect<Api>>::get();
        if constexpr (meta::same_as<A, dyn_tag>) {
            // delegate for dyn
            constexpr auto apis = detail::to_dyn(req_apis);
            return apis;
        } else if constexpr (meta::special_of<Delegate, in_class_tag>) {
            // delegate for ImplInClass
            using inner_t       = typename Delegate::type;
            constexpr auto apis = detail::FromCollect<TCollect<inner_t>>::get();
            detail::check_apis<Api, decltype(req_apis), decltype(apis)>();
            return apis;
        } else {
            // delegate for Impl
            constexpr auto apis = detail::FromCollect<TCollect<Impl<T, A>>>::get();
            detail::check_apis<Api, decltype(req_apis), decltype(apis)>();
            return apis;
        }
    }

    static constexpr auto apis { collect() };

    template<usize I, typename... Args>
    static constexpr decltype(auto) call(const Api* self, Args&&... args) {
        if constexpr (meta::same_as<A, dyn_tag>) {
            // delegate for dyn
            auto dyn = static_cast<const Dyn<T>*>(self);
            return dyn->apis->template get<I>()(dyn->self, std::forward<Args>(args)...);
        } else if constexpr (meta::special_of<Delegate, in_class_tag>) {
            // delegate for ImplInClass
            auto self_ = static_cast<ImplInClass<T, A> const*>(self)->_self;
            return std::invoke(apis.template get<I>(), self_, std::forward<Args>(args)...);
        } else {
            // delegate for Impl
            Impl<T, A> self_ { static_cast<const A*>(self) };
            return std::invoke(apis.template get<I>(), self_, std::forward<Args>(args)...);
        }
    }

    template<usize I, typename... Args>
    static constexpr decltype(auto) call(Api* self, Args&&... args) {
        if constexpr (std::same_as<A, dyn_tag>) {
            // delegate for dyn
            auto dyn = static_cast<const Dyn<T>*>(self);
            return dyn->apis->template get<I>()(dyn->self, std::forward<Args>(args)...);
        } else if constexpr (meta::special_of<Delegate, in_class_tag>) {
            // delegate for ImplInClass
            auto self_ = const_cast<A*>(static_cast<ImplInClass<T, A>*>(self)->_self);
            return std::invoke(apis.template get<I>(), self_, std::forward<Args>(args)...);
        } else {
            // delegate for Impl
            Impl<T, A> self_ { static_cast<const A*>(self) };
            return std::invoke(apis.template get<I>(), self_, std::forward<Args>(args)...);
        }
    }

    template<usize I, typename... Args>
    static constexpr decltype(auto) call_static(Args&&... args) {
        return apis.template get<I>()(std::forward<Args>(args)...);
    }
};

export template<usize I, typename TApi, typename... Args>
    requires IsTraitApi<TApi>
constexpr decltype(auto) trait_call(TApi* self, Args&&... args) {
    return TraitMeta<
        typename TApi::Trait,
        typename detail::ApiInner<TApi>::type,
        typename detail::ApiInner<TApi>::delegate_type>::template call<I>(self,
                                                                          std::forward<Args>(
                                                                              args)...);
}

export template<usize I, typename TApi, typename... Args>
    requires IsTraitApi<TApi>
constexpr decltype(auto) trait_static_call(Args&&... args) {
    return TraitMeta<typename TApi::Trait,
                     typename detail::ApiInner<TApi>::type,
                     typename detail::ApiInner<TApi>::delegate_type>::
        template call<I>(std::forward<Args>(args)...);
}

template<typename T>
class Dyn : public meta::remove_cv_t<T>::template Api<dyn_tag> {
    using raw_t = meta::remove_cv_t<T>;
    using M     = TraitMeta<raw_t, dyn_tag>;
    friend M;
    using ptr_t = std::conditional_t<meta::is_const_v<T>, const_voidp, voidp>;

    const decltype(M::apis)* const apis;
    ptr_t                          self;

    template<typename U>
    struct Convert {
        using UM = TraitMeta<raw_t, U>;
        template<usize I, typename Fn>
        struct Wrap;
        template<usize I, typename Ret, bool Ne, typename... Args>
        struct Wrap<I, Ret (*)(Args...) noexcept(Ne)> {
            consteval static auto get() { return UM::apis.template get<I>(); }
        };
        template<usize I, typename Ret, bool Ne, typename... Args>
        struct Wrap<I, Ret (*)(voidp, Args...) noexcept(Ne)> {
            consteval static auto get() {
                return [](voidp p, Args... args) {
                    Impl<meta::remove_cv_t<T>, U> self { static_cast<const U*>(p) };
                    return std::invoke(
                        UM::apis.template get<I>(), self, std::forward<Args>(args)...);
                };
            }
        };

        template<std::size_t... Is>
        consteval static auto convert(std::index_sequence<Is...>) {
            return decltype(M::apis) { (Wrap<Is, decltype(M::apis.template get<Is>())>::get())... };
        }

        constexpr static decltype(M::apis) apis { convert(
            std::make_index_sequence<decltype(M::apis)::size()> {}) };
    };

public:
    template<typename U>
    Dyn(U* p) noexcept: apis(&Convert<std::remove_cv_t<U>>::apis), self(static_cast<ptr_t>(p)) {}

    template<typename U>
    Dyn(U& p) noexcept
        : apis(&Convert<std::remove_cv_t<U>>::apis), self(static_cast<ptr_t>(std::addressof(p))) {}

    Dyn(const Dyn&) noexcept = default;
    Dyn(Dyn&&) noexcept      = default;

    Dyn& operator=(const Dyn&) noexcept = default;
    Dyn& operator=(Dyn&&) noexcept      = default;
};

export template<typename T, typename A>
auto make_dyn(A* t) {
    using dyn_t = std::conditional_t<std::is_const_v<A>, Dyn<const T>, Dyn<T>>;
    return dyn_t(t);
}

export template<typename T, typename A>
auto make_dyn(A& t) {
    return make_dyn<T, A>(std::addressof(t));
}

template<typename T, typename A>
struct ImplInClass : meta::remove_cv_t<T>::template Api<A, in_class_tag<A>> {
    A const* _self;

protected:
    auto self() -> A& { return *const_cast<A*>(_self); }
    auto self() const -> A const& { return *_self; }
};

///
/// @brief as a triat
// only accept lvalue for no stack-use-after-scope
// requires Impled<T, meta::remove_cvref_t<A>>, maybe used in impldef which not satisfy
export template<typename T, typename A>
auto as(A& t) {
    using impl_t = Impl<T, meta::remove_cvref_t<A>>;
    using ret_t  = meta::conditional_t<meta::is_const_v<A>, meta::add_const_t<impl_t>, impl_t>;
    impl_t i {};
    i._self = std::addressof(t);
    return ret_t { i };
}

export template<typename Self, typename... Traits>
struct WithTrait : Traits::template Api<Self>... {
    constexpr bool operator==(const WithTrait) const { return true; }
};

export template<typename Self, typename... Traits>
struct WithTraitDefault : ImplDefault<Traits, Self, DefPolicy::InClass>... {
    constexpr bool operator==(const WithTraitDefault) const { return true; }
};

export template<typename Self, typename... Traits>
struct MayWithTrait
    : meta::conditional_t<Impled<Traits, Self>, typename Traits::template Api<Self>, Empty>... {
    constexpr bool operator==(const MayWithTrait) const { return true; }
};

} // namespace rstd
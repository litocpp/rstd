module;
#include <tuple>
export module rstd.core:trait;
export import :basic;
export import :meta;

namespace rstd
{

export class TraitPtr;

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

template<template<typename> class T, typename A>
struct ApiInner<T<A>> {
    using type = A;
};

template<template<typename> class T, typename A>
struct ApiInner<const T<A>> {
    using type = A;
};

template<typename T>
struct FuncTraits;

template<typename TApi, typename TApiB, std::size_t Index = 0>
consteval void check_apis() {
    if constexpr (Index == 0) {
        static_assert(TApi::size() == TApiB::size(), "Please implement all Trait api");
    }

    if constexpr (Index < TApi::size()) {
        using T1 = TApi::template type_at<Index>;
        using T2 = TApiB::template type_at<Index>;

        static_assert(std::is_same_v<T1, T2>, "Trait api not satisfy");
        check_apis<TApi, TApiB, Index + 1>();
    }
}

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) noexcept(Ne)> {
    template<typename F>
    using to = Ret (F::*)(Args...) noexcept(Ne);

    template<typename F = T*>
    using to_static = Ret (*)(F, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct FuncTraits<Ret (T::*)(Args...) const noexcept(Ne)> {
    template<typename F>
    using to = Ret (F::*)(Args...) const noexcept(Ne);

    template<typename F = T*>
    using to_static =
        Ret (*)(std::conditional_t<std::is_pointer_v<F>,
                                   std::add_pointer_t<std::add_const_t<std::remove_pointer_t<F>>>,
                                   std::add_const_t<F>>,
                Args...) noexcept(Ne);
};

template<typename Api>
using to_static_t = FuncTraits<Api>::template to_static<TraitPtr>;

template<typename... Api>
consteval auto to_static(Apis<Api...> in) {
    return Apis { to_static_t<Api>(nullptr)... };
}

} // namespace detail

export template<template<typename, typename...> class T, typename... Args>
struct TTrait {
    template<typename U>
    using type = T<U, Args...>;
};

class TraitPtr {
    void const* const p;

public:
    template<typename T>
    TraitPtr(const TraitPtr& o) noexcept: p(o.p) {}
    template<typename T>
    TraitPtr(T* t) noexcept: p(t) {}

    template<typename T>
    constexpr auto cast() noexcept -> T* {
        return static_cast<T*>(p);
    }

    template<typename T>
    constexpr auto cast() const noexcept -> const T* {
        return static_cast<const T*>(p);
    }

    template<typename T>
    constexpr auto as_ref() noexcept -> T& {
        return *static_cast<T*>(const_cast<void*>(p));
    }

    template<typename T>
    constexpr auto as_ref() const noexcept -> const T& {
        return *static_cast<const T*>(p);
    }
};

export template<typename T, typename A>
struct Impl;

// namespace detail
// {
// template<typename T>
// struct impl_helper;
// template<template<typename...> class T, typename A, typename... Args>
// struct impl_helper<T<A, Args...>> {
//     using type = Impl<TTrait<T, Args...>::template type, A>;
//     using Self = A;
//
//     template<template<typename...> class F>
//     using to = F<A, Args...>;
// };
//
// } // namespace detail

struct DynImpl {};

export template<typename A, typename... T>
concept Impled = (std::semiregular<Impl<T, A>> && ...) || meta::same_as<A, DynImpl>;

export template<typename T>
concept IsTraitApi = requires() {
    typename T::Trait;
    typename detail::ApiInner<T>::type;
};

export enum class ConstNess { Mutable, Const };

export template<typename T>
struct Dyn;

export template<typename T>
struct Def {};

export template<typename T, typename A>
using DefImpl = Impl<T, Def<A>>;

export template<typename T, typename A>
struct TraitMeta {
    using Api = typename T::template Api<A>;
    template<typename F>
    using TCollect = typename T::template TCollect<F>;

    static consteval auto collect() {
        constexpr auto req_apis = detail::to_static(detail::FromCollect<TCollect<Api>>::get());
        if constexpr (std::same_as<A, DynImpl>) {
            return req_apis;
        } else {
            constexpr auto apis = detail::FromCollect<TCollect<Impl<T, A>>>::get();
            detail::check_apis<decltype(req_apis), decltype(apis)>();
            return apis;
        }
    }

    static constexpr auto apis { collect() };

    template<usize I, typename... Args>
    static constexpr auto call(const Api* self, Args&&... args) {
        if constexpr (std::same_as<A, DynImpl>) {
            auto dyn = static_cast<const Dyn<T>*>(self);
            return dyn->apis->template get<I>()(dyn->self, std::forward<Args>(args)...);
        } else {
            return apis.template get<I>()(self, std::forward<Args>(args)...);
        }
    }

    template<usize I, typename... Args>
    static constexpr auto call(Api* self, Args&&... args) {
        if constexpr (std::same_as<A, DynImpl>) {
            auto dyn = static_cast<const Dyn<T>*>(self);
            return dyn->apis->template get<I>()(dyn->self, std::forward<Args>(args)...);
        } else {
            return apis.template get<I>()(self, std::forward<Args>(args)...);
        }
    }

    template<usize I, typename... Args>
    static constexpr auto call_static(Args&&... args) {
        return apis.template get<I>()(std::forward<Args>(args)...);
    }
};

export template<usize I, typename TApi, typename... Args>
    requires IsTraitApi<TApi>
constexpr auto trait_call(TApi* self, Args&&... args) {
    return TraitMeta<typename TApi::Trait, typename detail::ApiInner<TApi>::type>::template call<I>(
        self, std::forward<Args>(args)...);
}

export template<usize I, typename TApi, typename... Args>
    requires IsTraitApi<TApi>
constexpr auto trait_static_call(Args&&... args) {
    return TraitMeta<typename TApi::Trait, typename detail::ApiInner<TApi>::type>::template call<I>(
        std::forward<Args>(args)...);
}

template<typename T>
class Dyn : public meta::remove_cv_t<T>::template Api<DynImpl> {
    using raw_t = meta::remove_cv_t<T>;
    using M     = TraitMeta<raw_t, DynImpl>;
    friend M;
    using ptr_t = std::conditional_t<meta::is_const_v<T>, const_voidp, voidp>;

    const decltype(M::apis)* const apis;
    ptr_t                          self;

public:
    template<typename U>
    Dyn(U* p) noexcept
        : apis(&TraitMeta<raw_t, std::remove_cv_t<U>>::apis), self(static_cast<ptr_t>(p)) {}

    template<typename U>
    Dyn(U& p) noexcept
        : apis(&TraitMeta<raw_t, std::remove_cv_t<U>>::apis),
          self(static_cast<ptr_t>(std::addressof(p))) {}

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

export template<typename Self, typename... Traits>
struct WithTrait : public Traits::template Api<Self>... {};

export template<typename Self, typename... Traits>
struct MayWithTrait : public meta::conditional_t<Impled<Traits, Self>,
                                                 typename Traits::template Api<Self>, Empty>... {};

} // namespace rstd
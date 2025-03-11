module;
#include <tuple>
export module rstd.trait:core;

namespace rstd
{

export class TraitPtr;
export template<typename... Api>
struct TraitApi {
    std::tuple<Api...> apis;

    constexpr TraitApi(Api... api) noexcept: apis(api...) {}

    template<std::size_t I>
    constexpr auto get() const noexcept {
        return std::get<I>(apis);
    }
    template<std::size_t I>
    using type_at = std::tuple_element_t<I, decltype(apis)>;

    consteval static auto size() noexcept { return std::tuple_size<decltype(apis)> {}; }
};

namespace detail
{

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

template<typename T>
struct MemberFuncTraits;
template<typename T, typename Ret, typename... Args, bool Ne>
struct MemberFuncTraits<Ret (T::*)(Args...) noexcept(Ne)> {
    template<typename F>
    using to = Ret (F::*)(Args...) noexcept(Ne);

    template<typename F = T*>
    using to_static = Ret (*)(F, Args...) noexcept(Ne);
};

template<typename T, typename Ret, typename... Args, bool Ne>
struct MemberFuncTraits<Ret (T::*)(Args...) const noexcept(Ne)> {
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
using to_static_t = MemberFuncTraits<Api>::template to_static<TraitPtr>;

template<typename... Api>
consteval auto to_impl(TraitApi<Api...> in) {
    return TraitApi { to_static_t<Api>(nullptr)... };
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
    auto cast() noexcept -> T* {
        return static_cast<T*>(p);
    }

    template<typename T>
    auto cast() const noexcept -> const T* {
        return static_cast<const T*>(p);
    }

    template<typename T>
    auto as_ref() noexcept -> T& {
        return *static_cast<T*>(const_cast<void*>(p));
    }

    template<typename T>
    auto as_ref() const noexcept -> const T& {
        return *static_cast<const T*>(p);
    }
};

export template<template<typename> class T, typename A>
struct Impl;

export template<typename A, template<typename> class... T>
concept Implemented = (std::semiregular<Impl<T, A>> && ...);

template<template<typename> class T>
struct DynImpl {};

export enum class ConstNess { Mutable, Const };

export template<template<typename> class T, ConstNess Cn = ConstNess::Mutable>
struct Dyn;

export template<typename T>
struct Def {};

export template<template<typename> class T, typename F>
using DefImpl = Impl<T, Def<F>>;

export template<template<typename> class T, typename A>
struct TraitMeta {
    static consteval auto collect() {
        constexpr auto req_apis = detail::to_impl(T<A>::template collect<T<A>>());
        if constexpr (std::same_as<A, DynImpl<T>>) {
            return req_apis;
        } else {
            constexpr auto apis = T<A>::template collect<Impl<T, A>>();
            detail::check_apis<decltype(req_apis), decltype(apis)>();
            return apis;
        }
    }
    static constexpr auto apis { collect() };

    template<int I, typename... Args>
    static auto call(const T<A>* self, Args&&... args) {
        if constexpr (std::same_as<A, DynImpl<T>>) {
            auto dyn = static_cast<const Dyn<T>*>(self);
            return dyn->apis->template get<I>()(dyn->self, std::forward<Args>(args)...);
        } else {
            return apis.template get<I>()(TraitPtr(self), std::forward<Args>(args)...);
        }
    }
    template<int I, typename... Args>
    static auto call_static(Args&&... args) {
        return apis.template get<I>()(std::forward<Args>(args)...);
    }
};

export template<template<typename> class Tr, typename T>
auto make_dyn(T* t);

template<template<typename> class Tr, ConstNess Cn>
class Dyn : public Tr<DynImpl<Tr>> {
    using M = TraitMeta<Tr, DynImpl<Tr>>;
    friend M;
    template<template<typename> class, typename T>
    friend auto make_dyn(T* t);
    using ptr_t = std::conditional_t<Cn == ConstNess::Const, const TraitPtr, TraitPtr>;

    const decltype(M::apis)* const apis;
    ptr_t                          self;

public:
    template<typename T>
        requires(Cn == ConstNess::Const || ! std::is_const_v<T>)
    Dyn(T* p) noexcept: apis(&TraitMeta<Tr, std::remove_cv_t<T>>::apis), self(p) {}

    Dyn(const Dyn&) noexcept = default;
    Dyn(Dyn&&) noexcept      = default;

    Dyn& operator=(const Dyn&) noexcept = default;
    Dyn& operator=(Dyn&&) noexcept      = default;
};

template<template<typename> class Tr, typename T>
auto make_dyn(T* t) {
    using dyn_t = std::
        conditional_t<std::is_const_v<T>, Dyn<Tr, ConstNess::Const>, Dyn<Tr, ConstNess::Mutable>>;
    return dyn_t(t);
}

export template<template<typename> class Tr, typename T>
auto make_dyn(T& t) {
    return make_dyn<Tr, T>(&t);
}

export template<typename Derived, template<typename> class... Traits>
struct WithTrait : public Traits<Derived>... {};

} // namespace rstd
module;
#include <utility>
#include "rstd/trait_macro.hpp"
export module rstd.core:ops.function;
export import :trait;

namespace rstd
{
export template<typename Self, typename T>
struct FnOnce;

export template<typename Self, typename R, typename... Args>
struct FnOnce<Self, R(Args...)> {
    using Output = R;

    auto call_once(Args... args) -> R;
    template<typename T>
    using FnOnceT = FnOnce<T, R(Args...)>;

    TRAIT(FnOnceT, &F::call_once)
};

export template<typename Self, typename T>
struct FnMut;

export template<typename Self, typename R, typename... Args>
    requires ImplementedT<FnOnce<Self, R(Args...)>>
struct FnMut<Self, R(Args...)> {
    auto call_mut(Args... args) -> R;

    template<typename T>
    using FnMutT = FnMut<T, R(Args...)>;

    TRAIT(FnMutT, &F::call_mut)
};

export template<typename Self, typename T>
struct Fn;

export template<typename Self, typename R, typename... Args>
    requires ImplementedT<FnMut<Self, R(Args...)>>
struct Fn<Self, R(Args...)> {
    auto call(Args... args) const -> R;

    template<typename T>
    using FnT = Fn<T, R(Args...)>;
    TRAIT(FnT, &F::call)
};

namespace detail
{
template<template<typename...> class Tr, typename T>
struct fn_helper : meta::false_type {};

template<typename Self, typename R, typename... Args>
struct fn_helper<FnOnce, FnOnce<Self, R(Args...)>> : meta::true_type {
    using Output = R;
    static auto call_once(TraitPtr self, Args... args) -> Output {
        return self.as_ref<Self>(std::forward<Args>(args)...);
    }
};

template<typename Self, typename R, typename... Args>
struct fn_helper<FnMut, FnMut<Self, R(Args...)>> : meta::true_type {
    using Output = R;
    static auto call_mut(TraitPtr self, Args... args) -> Output {
        return self.as_ref<Self>(std::forward<Args>(args)...);
    }
};

template<typename Self, typename R, typename... Args>
struct fn_helper<Fn, Fn<Self, R(Args...)>> : meta::true_type {
    using Output = R;
    static auto call(const TraitPtr self, Args... args) -> Output {
        return self.as_ref<const Self>(std::forward<Args>(args)...);
    }
};

} // namespace detail

export template<template<typename> class T, typename Self>
    requires(detail::fn_helper<FnOnce, T<Self>>::value)
struct Impl<T, Self> : detail::fn_helper<FnOnce, T<Self>> {};

export template<template<typename> class T, typename Self>
    requires(detail::fn_helper<FnMut, T<Self>>::value)
struct Impl<T, Self> : detail::fn_helper<FnMut, T<Self>> {};

export template<template<typename> class T, typename Self>
    requires(detail::fn_helper<Fn, T<Self>>::value)
struct Impl<T, Self> : detail::fn_helper<Fn, T<Self>> {};

} // namespace rstd
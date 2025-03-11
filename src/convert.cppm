module;
#include <concepts>
#include <utility>
#include "trait/macro.hpp"
export module rstd.trait:convert;
import :core;

namespace rstd::convert
{

export template<typename Self, typename TF>
struct From {
    using from_t = TF;
    static auto from(from_t value) -> Self { M::template call_static<0>(value); }

    template<typename T>
    using TFrom = From<T, TF>;

    TRAIT(TFrom, &F::from)
};

export template<typename Self, typename TF>
struct Into {
    using into_t = TF;

    auto into() -> into_t { return M::template call<0>(this); }

    template<typename T>
    using TInto = Into<T, TF>;

    TRAIT(TInto, &F::into)
};

export template<typename T, typename F>
auto into(F&& val) -> T {
    return Impl<TTrait<Into, T>::template type, std::remove_reference_t<F>>::into(val);
}

} // namespace rstd::convert

namespace rstd
{

export template<template<typename> class T, typename Self>
    requires std::same_as<T<Self>, convert::Into<Self, typename T<Self>::into_t>> &&
             Implemented<typename T<Self>::into_t, TTrait<convert::From, Self>::template type>
struct Impl<T, Self> {
    using into_t = typename T<Self>::into_t;
    static auto into(TraitPtr self) -> typename T<Self>::into_t {
        return Impl<TTrait<convert::From, Self>::template type, into_t>::from(self.as_ref<Self>());
    }
};
} // namespace rstd
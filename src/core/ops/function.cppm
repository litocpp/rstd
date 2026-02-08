export module rstd.core:ops.function;
export import :trait;

namespace rstd
{
export template<typename T>
struct FnOnce {
    static_assert(false);
};

template<typename R, bool NoEx, typename... Args>
struct FnOnce<R(Args...) noexcept(NoEx)> {
    using Output = R;
    template<typename Self, typename = void>
    struct Api {
        auto call_once(Args... args) noexcept(NoEx) -> R;
    };
    template<typename T>
    using Funcs = TraitFuncs<&T::call_once>;
};

export template<typename T>
struct FnMut {
    static_assert(false);
};

template<typename R, bool NoEx, typename... Args>
struct FnMut<R(Args...) noexcept(NoEx)> {
    template<typename Self, typename = void>
        requires Impled<Self, FnOnce<R(Args...) noexcept(NoEx)>>
    struct Api {
        auto call_mut(this Api& self, Args... args) noexcept(NoEx) -> R;
    };
    template<typename T>
    using Funcs = TraitFuncs<&T::call_mut>;
};

export template<typename T>
struct Fn {
    static_assert(false);
};

template<typename R, bool NoEx, typename... Args>
struct Fn<R(Args...) noexcept(NoEx)> {
    template<typename Self, typename = void>
        requires Impled<Self, FnMut<R(Args...) noexcept(NoEx)>>
    struct Api {
        auto call(this const Api& self, Args... args) noexcept(NoEx) -> R;
    };
    template<typename T>
    using Funcs = TraitFuncs<&T::call>;
};

} // namespace rstd
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
        using Trait = FnOnce;

        // auto call_once(Args... args) noexcept(NoEx) -> R;
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
    struct Api {
        using Trait = FnMut;

        auto operator()(Args... args) noexcept(NoEx) -> R {
            return trait_call<0>(this, rstd::forward<Args>(args)...);
        }
    };
    template<typename T>
    using Funcs = TraitFuncs<&T::operator()>;

    static constexpr bool direct { true };
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
        using Trait = Fn;

        auto operator()(Args... args) const noexcept(NoEx) -> R;
    };
    template<typename T>
    using Funcs = TraitFuncs<&T::operator()>;

    static constexpr bool direct { true };
};

} // namespace rstd
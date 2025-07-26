export module rstd.core:ops.function;
export import :trait;

namespace rstd
{
export template<typename T>
struct FnOnce;

export template<typename R, typename... Args>
struct FnOnce<R(Args...)> {
    using Output = R;
    template<typename Self>
    struct Api {
        auto call_once(Args... args) -> R;
    };
    template<typename T>
    using TCollect = TraitCollect<&T::call_once>;
};

export template<typename T>
struct FnMut;

export template<typename R, typename... Args>
struct FnMut<R(Args...)> {
    template<typename Self>
        requires Impled<Self, FnOnce<R(Args...)>>
    struct Api {
        auto call_mut(Args... args) -> R;
    };
    template<typename T>
    using TCollect = TraitCollect<&T::call_mut>;
};

export template<typename T>
struct Fn;

export template<typename R, typename... Args>
struct Fn<R(Args...)> {
    template<typename Self>
        requires Impled<Self, FnMut<R(Args...)>>
    struct Api {
        auto call(Args... args) const -> R;
    };
    template<typename T>
    using TCollect = TraitCollect<&T::call>;
};

} // namespace rstd
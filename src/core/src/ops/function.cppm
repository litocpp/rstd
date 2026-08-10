export module rstd.core:ops.function;
export import :trait;

namespace rstd
{
template<typename F, typename R, bool NoEx, typename... Args>
consteval auto once_callable() -> bool {
    if constexpr (! requires { mtp::declval<F>()(mtp::declval<Args>()...); }) {
        return false;
    } else if constexpr (! mtp::same_as<mtp::invoke_result_t<F, Args...>, R>) {
        return false;
    } else if constexpr (NoEx) {
        return noexcept(mtp::declval<F>()(mtp::declval<Args>()...));
    } else {
        return true;
    }
}

template<typename F, typename R, bool NoEx, typename... Args>
consteval auto once_owned_callable() -> bool {
    return once_callable<F&&, R, NoEx, Args...>() || once_callable<F&, R, NoEx, Args...>();
}

template<typename R, bool NoEx, typename F, typename... Args>
    requires(once_callable<F, R, NoEx, Args...>())
constexpr auto invoke_once(F&& callable, Args&&... args) noexcept(NoEx) -> R {
    return rstd::forward<F>(callable)(rstd::forward<Args>(args)...);
}

/// Trait for callables that can be called once, consuming themselves.
/// \tparam T A function signature, e.g. `R(Args...)`.
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

        auto call_once(Args... args) noexcept(NoEx) -> R {
            return trait_call<0>(this, rstd::forward<Args>(args)...);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::call_once>;
};

template<typename R, bool NoEx, typename... Args, typename F>
    requires(once_owned_callable<F, R, NoEx, Args...>())
struct Impl<FnOnce<R(Args...) noexcept(NoEx)>, F> : ImplBase<F> {
    auto call_once(Args... args) noexcept(NoEx) -> R {
        if constexpr (once_callable<F&&, R, NoEx, Args...>()) {
            return invoke_once<R, NoEx>(rstd::move(this->self()), rstd::forward<Args>(args)...);
        } else {
            return invoke_once<R, NoEx>(this->self(), rstd::forward<Args>(args)...);
        }
    }
};

template<typename Signature>
struct InvokeOnce {
    static_assert(false);
};

template<typename R, bool NoEx, typename... Args>
struct InvokeOnce<R(Args...) noexcept(NoEx)> {
    template<typename F>
        requires(once_callable<F, R, NoEx, Args...>())
    static constexpr auto call(F&& callable, Args... args) noexcept(NoEx) -> R {
        return invoke_once<R, NoEx>(rstd::forward<F>(callable), rstd::forward<Args>(args)...);
    }
};

export template<typename Signature, typename F, typename... Args>
    requires requires(F&& callable, Args&&... args) {
        InvokeOnce<Signature>::call(rstd::forward<F>(callable), rstd::forward<Args>(args)...);
    }
constexpr decltype(auto) invoke_once(F&& callable, Args&&... args) noexcept(noexcept(
    InvokeOnce<Signature>::call(rstd::forward<F>(callable), rstd::forward<Args>(args)...))) {
    return InvokeOnce<Signature>::call(rstd::forward<F>(callable), rstd::forward<Args>(args)...);
}

/// Trait for callables that can be called by mutable reference.
/// \tparam T A function signature, e.g. `R(Args...)`.
export template<typename T>
struct FnMut {
    static_assert(false);
};

template<typename R, bool NoEx, typename... Args>
struct FnMut<R(Args...) noexcept(NoEx)> {
    static constexpr bool allow_const_member_impl { true };
    using SuperTraits = TraitList<FnOnce<R(Args...) noexcept(NoEx)>>;

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

/// Trait for callables that can be called by const reference.
/// \tparam T A function signature, e.g. `R(Args...)`.
export template<typename T>
struct Fn {
    static_assert(false);
};

template<typename R, bool NoEx, typename... Args>
struct Fn<R(Args...) noexcept(NoEx)> {
    using SuperTraits = TraitList<FnMut<R(Args...) noexcept(NoEx)>>;

    template<typename Self, typename = void>
        requires Impled<Self, FnMut<R(Args...) noexcept(NoEx)>>
    struct Api {
        using Trait = Fn;

        auto operator()(Args... args) const noexcept(NoEx) -> R {
            return trait_call<0>(this, rstd::forward<Args>(args)...);
        }
    };
    template<typename T>
    using Funcs = TraitFuncs<&T::operator()>;

    static constexpr bool direct { true };
};

} // namespace rstd

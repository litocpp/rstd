module;
#include <concepts>
#include <utility>
#include "rstd/trait_macro.hpp"
export module rstd.core:convert;
export import :trait;

namespace rstd::convert
{

export template<typename TF>
struct From {
    using from_t = TF;
    template<typename Self>
    struct Api {
        static auto from(from_t value) -> Self { trait_call_static<0, From<TF>, Self>(value); }
    };
    template<typename T>
    using TCollect = TraitCollect<&T::from>;
};

export template<typename TF>
struct Into {
    using into_t = TF;
    template<typename Self>
    struct Api {
        auto into() -> into_t { return trait_call<0>(this); }
    };

    template<typename T>
    using TCollect = TraitCollect<&T::into>;
};

export template<typename T, typename F>
auto into(F&& val) -> T {
    return Impl<Into<T>, std::remove_reference_t<F>>::into(val);
}

} // namespace rstd::convert

namespace rstd
{

export template<typename T, typename Self>
    requires std::same_as<T, convert::Into<typename T::into_t>> &&
             Impled<typename T::into_t, typename convert::From<Self>>
struct Impl<T, Self> : ImplBase<Self> {
    using into_t = typename T::into_t;
    auto into() -> into_t {
        return Impl<convert::From<Self>, into_t>::from(std::move(this->self()));
    }
};

template<typename T>
struct IntoWrapper {
    T self;
    template<typename U>
        requires Impled<meta::remove_cv_t<U>, convert::From<T>>
    operator U() {
        return Impl<convert::From<T>, meta::remove_cv_t<U>>::from(std::move(self));
    }

    IntoWrapper(T&& t): self(std::move(t)) {}
    IntoWrapper(const IntoWrapper&)            = delete;
    IntoWrapper& operator=(const IntoWrapper&) = delete;
    IntoWrapper(IntoWrapper&&)                 = default;
    IntoWrapper& operator=(IntoWrapper&&)      = default;
};

export template<typename T>
auto into(T t) -> IntoWrapper<meta::remove_reference_t<T>> {
    return { std::move(t) };
}

export template<typename T>
    requires(! meta::is_const_v<T> && ! meta::is_lvalue_reference_v<T>)
auto into(T&& t) -> IntoWrapper<meta::remove_reference_t<T>> {
    return { std::move(t) };
}

} // namespace rstd
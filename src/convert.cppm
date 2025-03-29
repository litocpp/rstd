module;
#include <concepts>
#include <utility>
#include "rstd/trait_macro.hpp"
export module rstd.convert;
export import rstd.core;

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
             Impled<typename T::into_t, typename convert::From<Self>::from_t>
struct Impl<T, Self> {
    using into_t = typename T::into_t;
    static auto into(Self* self) -> into_t { return Impl<convert::From<Self>, into_t>::from(self); }
};
} // namespace rstd
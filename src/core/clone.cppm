module;
#include "rstd/trait_macro.hpp"
#include <tuple>
export module rstd.core:clone;
export import :trait;

namespace rstd::clone
{

export struct Clone {
    template<typename Self>
    struct Api {
        using Trait = Clone;
        auto clone() const -> Self { return trait_call<0>(this); }

        void clone_from(Self& source) { return trait_call<1>(this, source); }
    };

    template<class T>
    using TCollect = TraitCollect<&T::clone, &T::clone_from>;
};

} // namespace rstd::clone

namespace rstd
{
export template<typename Self>
struct Impl<clone::Clone, Def<Self>> {
    static void clone_from(TraitPtr self, Self& source) {
        self.as_ref<Self>() = Impl<clone::Clone, Self>::clone(&source);
    }
};

export template<typename Self>
    requires meta::is_arithmetic_v<Self> || meta::is_pointer_v<Self> ||
             meta::is_copy_constructible_v<Self>
struct Impl<clone::Clone, Self> : DefImpl<clone::Clone, Self> {
    static auto clone(TraitPtr self) -> Self { return self.as_ref<Self>(); }
};

export template<typename Self>
    requires meta::is_specialization_of_v<Self, std::tuple> &&
             (! meta::is_copy_constructible_v<Self>)
struct Impl<clone::Clone, Self> : DefImpl<clone::Clone, Self> {
    static auto clone(TraitPtr ptr) -> Self {
        auto& self = ptr.as_ref<Self>();
        return std::apply(
            [](const auto&... elements) -> Self {
                return { rstd::Impl<rstd::clone::Clone, std::decay_t<decltype(elements)>>::clone(
                    &elements)... };
            },
            self);
    }
};

} // namespace rstd
module;
#include "rstd/trait_macro.hpp"
export module rstd.core:clone;
export import :trait;

namespace rstd::clone
{

export template<typename Self>
struct Clone {
    auto clone() const -> Self { return M::template call<0>(this); }

    void clone_from(Self& source) { return M::template call<1>(this, source); }

    TRAIT(Clone, &F::clone, &F::clone_from)
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
    requires meta::is_arithmetic_v<Self>
struct Impl<clone::Clone, Self> :  DefImpl<clone::Clone, Self> {
    static auto clone(TraitPtr self) -> Self {
        return self.as_ref<Self>();
    }
};
} // namespace rstd
module;
#include "trait/macro.hpp"
export module rstd.trait:clone;

import :core;

namespace rstd::clone
{

export template<typename Self>
struct Clone {
    auto clone() const -> Self { return M::template call<0>(); }

    void clone_from(Self& source) { return M::template call<1>(); }

    TRAIT(Clone, &F::clone, &F::clone_from)
};

} // namespace rstd::clone

namespace rstd
{
export template<typename Self>
struct Impl<clone::Clone, Def<Self>> {
    static void clone_from(TraitPtr self, Self& source) {}
};
} // namespace rstd
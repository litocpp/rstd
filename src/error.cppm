module;
#include <format>
#include "trait/macro.hpp"
export module rstd.trait:error;
import :core;
import :fmt;

namespace rstd::error
{
export template<typename Self>
    requires Implemented<Self, fmt::Display>
struct Error {
    auto source() -> Dyn<Error>* { return M::template call<0>(this); }
    // auto   provide(request: &mut Request) { ... }
    TRAIT(Error, &F::source)
};

} // namespace rstd::traits
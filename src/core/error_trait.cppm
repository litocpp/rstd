export module rstd.core:error.trait;
export import :trait;
export import :ptr.dyn;

export namespace rstd
{

namespace option
{
template<typename T>
class Option;
}

using option::Option;

namespace fmt
{
struct Debug;
struct Display;
} // namespace fmt

namespace error
{

struct Error;
using ErrorRef = ref<dyn<Error>>;

struct Error {
    using SuperTraits = TraitList<fmt::Debug, fmt::Display>;

    template<typename Self, typename Delegate = void>
        requires Impled<Self, fmt::Debug, fmt::Display>
    struct Api {
        using Trait = Error;

        auto source() const noexcept [[clang::lifetimebound]] -> Option<ErrorRef>;
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::source>;
};

} // namespace error
} // namespace rstd

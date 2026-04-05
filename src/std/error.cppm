export module rstd.error;
export import rstd.core;

namespace rstd::error
{
/// A trait representing the basic expectations for error values.
///
/// Types implementing this trait must also implement `fmt::Display`.
export struct Error {
    template<typename Self, typename = void>
        requires Impled<Self, fmt::Display>
    struct Api {
        using Trait = Error;
        //auto source() -> Dyn<Error> { return trait_call<0>(this); }
        // auto   provide(request: &mut Request) { ... }
    };
    // template<typename T>
    // using Funcs = TraitFuncs<&T::source>;
};

} // namespace rstd::error
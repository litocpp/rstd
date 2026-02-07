module;
export module rstd:alloc.str;
export import rstd.core;

namespace rstd::str_
{
export struct FromStr {
    template<typename Self, typename = void>
    struct Api {
        using Err = typename Impl<FromStr, Self>::Err;
        static auto from_str(ref<str> str) -> Result<Self, Err> {
            return rstd::trait_static_call<FromStr, Self>(str);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::from_str>;
};
} // namespace rstd::str_

namespace rstd
{
export template<typename T>
auto from_str(ref<str> str) {
    return Impl<str_::FromStr, meta::remove_cvref_t<T>>::from_str(str);
}
} // namespace rstd
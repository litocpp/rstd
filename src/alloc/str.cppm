module;
export module rstd.str;
export import rstd.core;

namespace rstd::str
{
export struct FromStr {
    template<typename Self, typename = void>
    struct Api {
        using Err = typename Impl<FromStr, Self>::Err;
        static auto from_str(ref_str str) -> Result<Self, Err> {
            return rstd::trait_static_call<FromStr, Self>(str);
        }
    };

    template<typename T>
    using TCollect = TraitCollect<&T::from_str>;
};
} // namespace rstd::str

namespace rstd
{
export template<typename T>
auto from_str(ref_str str) {
    return Impl<str::FromStr, meta::remove_cvref_t<T>>::from_str(str);
}
} // namespace rstd
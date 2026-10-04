export module rstd.core:random.source;
export import :prelude;
export import :convert;
export import :try_;

namespace rstd::random
{
export struct Rng {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Rng;
        using Error = typename Impl<Rng, Self>::Error;
        constexpr auto fill_bytes(mut_ref<u8[]> output) -> Result<empty, Error> {
            return trait_call<0>(this, output);
        }
    };
    template<typename T>
    using Funcs = TraitFuncs<&T::fill_bytes>;
};
} // namespace rstd::random

namespace rstd
{
template<typename T>
    requires requires(T& source, mut_ref<u8[]> output) {
        typename T::Error;
        { source.fill_bytes(output) } -> mtp::same_as<Result<empty, typename T::Error>>;
    }
struct Impl<random::Rng, T> : ImplBase<T> {
    using Error = typename T::Error;
    constexpr auto fill_bytes(mut_ref<u8[]> output) -> Result<empty, Error> {
        return this->self().fill_bytes(output);
    }
};
} // namespace rstd

namespace rstd::random
{
export template<typename R>
concept RandomSource = (! mtp::is_const<R>) && Impled<R, Rng>;

export template<typename R>
using rng_error_t = typename Impl<Rng, R>::Error;

export template<RandomSource R>
constexpr auto fill_bytes(R& source, mut_ref<u8[]> output) -> Result<empty, rng_error_t<R>> {
    if (output.is_empty()) return Ok(empty {});
    return as<Rng>(source).fill_bytes(output);
}

export template<typename D, typename R>
concept DistributionFor = RandomSource<R> && requires(D& distribution, R& source) {
    typename D::Output;
    { distribution.sample(source) } -> mtp::same_as<Result<typename D::Output, rng_error_t<R>>>;
};

export template<typename D, typename R>
    requires DistributionFor<mtp::rm_ref<D>, R>
constexpr auto sample(D&& distribution, R& source)
    -> Result<typename mtp::rm_ref<D>::Output, rng_error_t<R>> {
    return distribution.sample(source);
}
} // namespace rstd::random

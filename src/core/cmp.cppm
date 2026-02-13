export module rstd.core:cmp;
export import :trait;

namespace rstd::cmp
{

export template<typename Rhs>
struct PartialEq {
    template<typename Self, typename = void>
    struct Api {
        auto eq(const Rhs& other) noexcept -> bool { return trait_call<0>(this, other); }

        auto ne(const Rhs& other) noexcept -> bool { return trait_call<1>(this, other); }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::eq, &T::ne>;
};

} // namespace rstd::cmp

namespace rstd
{
template<typename Self, typename Rhs, auto P>
struct Impl<cmp::PartialEq<Rhs>, default_tag<Self, P>> : ImplBase<default_tag<Self, P>> {
    auto ne(const Rhs& other) noexcept -> bool {
        return ! as<cmp::PartialEq<Rhs>>(this->self()).eq();
    }
};
} // namespace rstd

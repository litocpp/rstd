export module rstd.core:hash;
export import :trait;

namespace rstd::hash
{

export struct Hash {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Hash;

        auto clone() const -> Self { return trait_call<0>(this); }

        void clone_from(Self& source) { return trait_call<1>(this, source); }
    };

    template<class T>
    using TCollect = TraitCollect<&T::clone, &T::clone_from>;
};

} // namespace rstd::hash
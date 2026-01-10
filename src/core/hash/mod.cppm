export module rstd.core:hash;
export import :trait;

namespace rstd::hash
{

export struct Hasher {};

export struct Hash {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Hash;

        template<typename H>
        auto hash(H& state);
    };
};

} // namespace rstd::hash
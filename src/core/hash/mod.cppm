export module rstd.core:hash;
export import :trait;

namespace rstd::hash
{

/// Trait representing a hashing algorithm's state.
export struct Hasher {};

/// Trait for types that can be hashed.
export struct Hash {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Hash;

        template<typename H>
        auto hash(H& state);
    };
};

} // namespace rstd::hash
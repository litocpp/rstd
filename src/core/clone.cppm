module;
#include <tuple>
export module rstd.core:clone;
export import :trait;

namespace rstd::clone
{

export struct Clone {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Clone;
        auto clone() const -> Self { return trait_call<0>(this); }

        void clone_from(Self& source) { return trait_call<1>(this, source); }
    };

    template<class T>
    using TCollect = TraitCollect<&T::clone, &T::clone_from>;
};

} // namespace rstd::clone

namespace rstd
{
export template<typename Self>
struct Impl<clone::Clone, Def<Self>> : ImplBase<Self> {
    void clone_from(Self& source) { this->self() = Impl<clone::Clone, Self> { &source }.clone(); }
};

export template<typename Self>
    requires meta::is_arithmetic_v<Self> || meta::is_pointer_v<Self> ||
             meta::is_copy_constructible_v<Self>
struct Impl<clone::Clone, Self> : ImplDefault<clone::Clone, Self> {
    auto clone() const -> Self { return this->self(); }
};

export template<typename Self>
    requires meta::is_specialization_of_v<Self, std::tuple> &&
             (! meta::is_copy_constructible_v<Self>)
struct Impl<clone::Clone, Self> : ImplDefault<clone::Clone, Self> {
    auto clone() const -> Self {
        auto& self = this->self();
        return std::apply(
            [](const auto&... elements) -> Self {
                return { rstd::as<rstd::clone::Clone>(elements).clone()... };
            },
            self);
    }
};

} // namespace rstd
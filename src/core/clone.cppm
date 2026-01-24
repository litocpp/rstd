export module rstd.core:clone;
export import :trait;

namespace rstd::clone
{

export struct Clone {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Clone;
        auto clone() const -> Self { return trait_call<0>(this); }

        void clone_from(Self& source) { return trait_call<1, Api>(this, source); }
    };

    template<class T>
    using TCollect = TraitCollect<&T::clone, &T::clone_from>;
};

} // namespace rstd::clone

namespace rstd
{
template<typename Self, auto P>
struct Impl<clone::Clone, default_tag<Self, P>> : ImplBase<default_tag<Self, P>> {
    void clone_from(Self& source) { this->self() = as<clone::Clone>(source).clone(); }
};

template<typename Self>
    requires meta::is_arithmetic_v<Self> || meta::is_pointer_v<Self> ||
             meta::is_copy_constructible_v<Self>
struct Impl<clone::Clone, Self> : ImplDefault<clone::Clone, Self> {
    auto clone() const -> Self { return this->self(); }
};

template<typename Self>
    requires meta::is_tuple_v<Self> && (! meta::is_copy_constructible_v<Self>)
struct Impl<clone::Clone, Self> : ImplDefault<clone::Clone, Self> {
    auto clone() const -> Self {
        auto& self = this->self();
        return cppstd::apply(
            [](const auto&... elements) -> Self {
                return { rstd::as<rstd::clone::Clone>(elements).clone()... };
            },
            self);
    }
};

} // namespace rstd
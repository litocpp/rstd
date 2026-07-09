export module rstd.core:clone;
export import :trait;

namespace rstd::clone
{

/// Trait for types that can explicitly duplicate themselves.
///
/// Implementors provide:
/// - `clone() const -> Self` : Creates and returns a duplicate of this value.
/// - `clone_from(Self& source)` : Overwrites self with a clone of source (optional, has default).
export struct Clone {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Clone;
        auto clone() const -> Self { return trait_call<0>(this); }

        void clone_from(Self& source) { return trait_call<1>(this, source); }
    };

    template<class T>
    using Funcs = TraitFuncs<&T::clone, &T::clone_from>;
};

} // namespace rstd::clone

namespace rstd
{
template<typename Tag>
    requires mtp::trait_default_tag<Tag>
struct Impl<clone::Clone, Tag> : ImplBase<Tag> {
    using Self = mtp::trait_default_self_t<Tag>;

    void clone_from(Self& source) { this->self() = as<clone::Clone>(source).clone(); }
};

template<typename Self>
    requires(! mtp::trait_default_tag<Self>) &&
            (mtp::is_arithmetic<Self> || mtp::is_ptr<Self> || mtp::copy<Self>)
struct Impl<clone::Clone, Self> : DefaultInImpl<clone::Clone, Self> {
    auto clone() const -> Self { return this->self(); }
};

template<typename Self>
    requires(! mtp::trait_default_tag<Self>) && mtp::is_tuple<Self> && (! mtp::copy<Self>)
struct Impl<clone::Clone, Self> : DefaultInImpl<clone::Clone, Self> {
    auto clone() const -> Self {
        auto& self = this->self();
        return rstd::apply(
            [](const auto&... elements) -> Self {
                return { rstd::as<rstd::clone::Clone>(elements).clone()... };
            },
            self);
    }
};

} // namespace rstd

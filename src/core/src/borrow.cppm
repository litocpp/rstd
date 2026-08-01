export module rstd.core:borrow;
export import :core;
export import :trait;

namespace rstd::borrow
{

/// Provides an immutable borrowed view of an owned value.
///
/// Implementations must preserve equality, ordering, and hashing between the owned value and the
/// returned view.
export template<typename T>
struct Borrow {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Borrow;

        auto borrow() const noexcept -> ref<T> { return trait_call<0>(this); }
    };

    template<typename Self>
    using Funcs = TraitFuncs<&Self::borrow>;
};

} // namespace rstd::borrow

namespace rstd
{

template<typename T>
struct Impl<borrow::Borrow<T>, T> : ImplBase<T> {
    auto borrow() const noexcept -> ref<T> {
        return ref<T>::from_raw_parts(rstd::addressof(this->self()));
    }
};

} // namespace rstd

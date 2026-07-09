export module rstd.core:cmp;
export import :trait;

namespace rstd::cmp
{

/// Trait for partial equality comparisons, analogous to Rust's `PartialEq`.
///
/// Implementors provide:
/// - `eq(const Rhs& other) noexcept -> bool` : Returns true if self equals other.
/// - `ne(const Rhs& other) noexcept -> bool` : Returns true if self does not equal other (has default).
/// \tparam Rhs The type to compare against.
export template<typename Rhs>
struct PartialEq {
    template<typename Self, typename = void>
    struct RequiredApi {
        using Trait = PartialEq;
        auto eq(const Rhs& other) const noexcept -> bool {
            return trait_required_call<0>(this, other);
        }
    };

    template<typename Self, typename = void>
    struct Api {
        using Trait = PartialEq;
        auto eq(const Rhs& other) const noexcept -> bool { return trait_call<0>(this, other); }

        auto ne(const Rhs& other) const noexcept -> bool { return trait_call<1>(this, other); }
    };

    template<typename T>
    using RequiredFuncs = TraitFuncs<&T::eq>;

    template<typename T>
    using Funcs = TraitFuncs<&T::eq, &T::ne>;
};

/// Returns the greater of two values.
/// \param v1 First value.
/// \param v2 Second value.
/// \return The larger of v1 and v2.
export template<typename T>
constexpr auto max(T v1, T v2) noexcept -> T {
    return v1 > v2 ? v1 : v2;
}

/// Returns the lesser of two values.
/// \param v1 First value.
/// \param v2 Second value.
/// \return The smaller of v1 and v2.
export template<typename T>
constexpr auto min(T v1, T v2) noexcept -> T {
    return v1 < v2 ? v1 : v2;
}

} // namespace rstd::cmp

namespace rstd
{
template<typename Rhs, typename Tag>
    requires mtp::trait_default_tag<Tag>
struct Impl<cmp::PartialEq<Rhs>, Tag> : ImplBase<Tag> {
    auto ne(const Rhs& other) const noexcept -> bool {
        return ! as<cmp::PartialEq<Rhs>>(this->self()).eq(other);
    }
};
} // namespace rstd

export module rstd.core:choice;

namespace rstd
{

/// A type-level wrapper that carries a compile-time value as a type.
/// \tparam V The compile-time value to wrap.
export template<auto V>
struct value_type {};

/// A tagged union that pairs an enum discriminant with associated data.
/// \tparam Enum The enum type used as the discriminant.
/// \tparam Data The data payload associated with the choice.
export template<typename Enum, typename Data>
struct Choice {
    using Entity = Enum;
    Entity entity;


};

} // namespace rstd
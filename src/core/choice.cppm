export module rstd.core:choice;

namespace rstd
{

export template<auto V>
struct value_type {};

export template<typename Enum, typename Data>
struct Choice {
    using Entity = Enum;
    Entity entity;


};

} // namespace rstd
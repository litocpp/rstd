export module rstd.core:tuple;
export import rstd.basic;

namespace rstd::tuple_
{

/// A fixed-size heterogeneous collection of values.
/// \tparam Ts The element types.
export template<typename... Ts>
class Tuple {};

} // namespace rstd::tuple
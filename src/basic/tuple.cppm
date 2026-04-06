export module rstd.basic:tuple;
export import :prelude;

namespace rstd::exp
{

/// A fixed-size heterogeneous collection of values.
/// \tparam Ts The element types.
export template<typename... Ts>
class tuple {};

} // namespace std::tuple
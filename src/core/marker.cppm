module;
export module rstd.core:marker;
export import :trait;

namespace rstd::marker
{

export template<typename T>
struct Copy {};

export template<typename T>
struct Send {};

export template<typename T>
struct Sync {};

} // namespace rstd::marker

namespace rstd
{
} // namespace rstd
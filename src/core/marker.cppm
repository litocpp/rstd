module;
export module rstd.core:marker;
export import :trait;

namespace rstd::marker
{

template<typename T>
struct Copy {};

} // namespace rstd::marker

namespace rstd
{

export template<typename Self>
    requires meta::is_arithmetic_v<Self> || meta::is_reference_v<Self> || meta::is_pointer_v<Self>
struct Impl<marker::Copy, Self> {};
} // namespace rstd
export module rstd.core:num.u32;
export import :option;
export import :trait;

namespace rstd
{
template<>
struct Impl<u32, u32> : ImplBase<u32> {
    // auto checked_add(u32 rhs) const noexcept -> Option<u32> {}
};
} // namespace rstd
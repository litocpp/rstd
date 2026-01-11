export module rstd.core:num.u32;
export import :option;
export import :trait;
export import :intrinsics;

namespace rstd
{
template<>
struct Impl<u32, u32> : ImplBase<u32> {
    using Self = u32;
    auto checked_add(Self rhs) const noexcept -> Option<u32> {
        auto [a, b] = overflowing_add(rhs);
        if (b) [[unlikely]] {
            return None();
        } else {
            return Some(a);
        }
    }

    auto overflowing_add(Self rhs) const noexcept -> cppstd::tuple<Self, bool> {
        return intrinsics::add_with_overflow(self(), rhs);
    }
};
} // namespace rstd
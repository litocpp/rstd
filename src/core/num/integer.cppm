module;
#define INT_IMPL(T) \
    template<>      \
    struct Impl<T, T> : IntImpl<T> {}

export module rstd.core:num.integer;
export import :option;
export import :trait;
export import :intrinsics;

namespace rstd
{
template<typename T>
struct IntImpl : ImplBase<T> {
    using Self = T;
    auto checked_add(Self rhs) const noexcept -> Option<Self> {
        auto [a, b] = overflowing_add(rhs);
        if (b) [[unlikely]] {
            return None();
        } else {
            return Some(a);
        }
    }

    auto overflowing_add(Self rhs) const noexcept -> cppstd::tuple<Self, bool> {
        return intrinsics::add_with_overflow(this->self(), rhs);
    }
};

INT_IMPL(u32);
INT_IMPL(u64);

} // namespace rstd
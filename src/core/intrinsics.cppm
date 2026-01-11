export module rstd.core:intrinsics;
export import :meta;

namespace rstd::intrinsics
{
template<class T>
    requires(meta::is_integral_v<T>)
constexpr auto add_with_overflow(T a, T b) noexcept -> cppstd::tuple<T, bool> {
    T    r {};
    bool of = __builtin_add_overflow(a, b, &r); // 对 signed/unsigned 都安全
    return { r, of };
}
} // namespace rstd::intrinsics
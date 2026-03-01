export module rstd.core:intrinsics;
export import :meta;

export namespace rstd::intrinsics
{

[[gnu::always_inline]] [[noreturn]] inline void abort() noexcept { __builtin_trap(); }

template<class T>
    requires(mtp::is_integral_v<T>)
[[gnu::always_inline]] inline constexpr auto add_with_overflow(T a, T b) noexcept
    -> cppstd::tuple<T, bool> {
    T    r {};
    bool of = __builtin_add_overflow(a, b, &r); // 对 signed/unsigned 都安全
    return { r, of };
}
} // namespace rstd::intrinsics
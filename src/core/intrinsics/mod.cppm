export module rstd.core:intrinsics;
export import rstd.basic;

export namespace rstd::intrinsics
{

/// Immediately aborts the process via a trap instruction.
[[gnu::always_inline]] [[noreturn]] inline void abort() noexcept { __builtin_trap(); }

/// Performs checked addition, returning the result and whether overflow occurred.
/// \tparam T An integer type.
/// \param a The first operand.
/// \param b The second operand.
/// \return A tuple of the wrapped result and a boolean that is true on overflow.
template<class T>
    requires(mtp::is_int<T>)
[[gnu::always_inline]] inline constexpr auto add_with_overflow(T a, T b) noexcept
    -> rstd::tuple<T, bool> {
    T    r {};
    bool of = __builtin_add_overflow(a, b, &r); // 对 signed/unsigned 都安全
    return { r, of };
}
} // namespace rstd::intrinsics
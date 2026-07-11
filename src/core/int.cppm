export module rstd.core:int_;
export import rstd.basic;

namespace rstd
{

#define RSTD_INT_NS(NS, T)                                       \
    export namespace NS                                          \
    {                                                            \
    inline constexpr T   MIN  = numeric_limits<T>::min();        \
    inline constexpr T   MAX  = numeric_limits<T>::max();        \
    inline constexpr u32 BITS = static_cast<u32>(8 * sizeof(T)); \
    }

RSTD_INT_NS(u8_, u8)
RSTD_INT_NS(u16_, u16)
RSTD_INT_NS(u32_, u32)
RSTD_INT_NS(u64_, u64)
RSTD_INT_NS(usize_, usize)

RSTD_INT_NS(i8_, i8)
RSTD_INT_NS(i16_, i16)
RSTD_INT_NS(i32_, i32)
RSTD_INT_NS(i64_, i64)
RSTD_INT_NS(isize_, isize)

#undef RSTD_INT_NS

export namespace u128_
{
inline constexpr u128 MIN  = 0;
inline constexpr u128 MAX  = ~u128(0);
inline constexpr u32  BITS = 128;
} // namespace u128_

export namespace i128_
{
inline constexpr i128 MAX  = static_cast<i128>(~u128(0) >> 1);
inline constexpr i128 MIN  = -MAX - 1;
inline constexpr u32  BITS = 128;
} // namespace i128_

} // namespace rstd

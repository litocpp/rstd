module;

#define ImplNonZero(T)                                                          \
    template<>                                                                  \
    struct NonZero<T> : NonZeroBase<T> {                                        \
        constexpr static auto make(T t) -> NonZero<T> { return { t }; }         \
        constexpr auto        unwrap() const -> T { return v; }                 \
        friend constexpr bool operator==(NonZero<T> a, NonZero<T> b) noexcept { \
            return a.v == b.v;                                                  \
        }                                                                       \
    }

#define impl_zeroable_primitive(T)

export module rstd.core:num.nonzero;
export import :core;

namespace rstd::num
{
template<typename T>
struct NonZeroBase {
    T    v;
    auto get() const noexcept -> T { return v; }
};

export struct ZeroablePrimitive {};

} // namespace rstd::num

export namespace rstd
{

};

export namespace rstd::num
{

/// A value that is known not to equal zero.
///
/// This enables some memory layout optimization.
/// For example, `Option<NonZero<u32>>` is the same size as `u32`:
template<typename T>
struct NonZero {
    static_assert(false);
};

ImplNonZero(i8);
ImplNonZero(i16);
ImplNonZero(i32);
ImplNonZero(i64);
ImplNonZero(u8);
ImplNonZero(u16);
ImplNonZero(u32);
ImplNonZero(u64);

} // namespace rstd::num
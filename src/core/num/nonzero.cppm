module;

#define ImplNonZero(Inner, T)                               \
    namespace rstd::num::nonzero                            \
    {                                                       \
    }                                                       \
    namespace rstd                                          \
    {                                                       \
    template<>                                              \
    struct Impl<rstd::num::nonzero::ZeroablePrimitive, T> { \
        using NonZeroInner = rstd::num::niche_types::Inner; \
    };                                                      \
    }

export module rstd.core:num.nonzero;
export import :core;
export import :num.niche_types;

namespace rstd::num::nonzero
{
struct ZeroablePrimitive {};

} // namespace rstd::num::nonzero

namespace rstd::num::nonzero
{

/// A value that is known not to equal zero.
///
/// This enables some memory layout optimization.
/// For example, `Option<NonZero<u32>>` is the same size as `u32`:
export template<typename T>
struct NonZero {
    static_assert(false);
};
} // namespace rstd::num::nonzero

namespace rstd::option::detail
{

template<typename T, typename NonePayload, typename SomePayload>
struct option_storage<num::nonzero::NonZero<T>, NonePayload, SomePayload>
    : zero_niche_option_storage<NonePayload, SomePayload> {
    using base = zero_niche_option_storage<NonePayload, SomePayload>;
    using base::base;

    static_assert(sizeof(SomePayload) == sizeof(num::nonzero::NonZero<T>));
};

} // namespace rstd::option::detail

namespace rstd::num::nonzero
{

template<typename T>
    requires Impled<T, ZeroablePrimitive>
struct NonZero<T> {
    using Self = NonZero;
    Impl<ZeroablePrimitive, T>::NonZeroInner val;

    constexpr static auto make(T n) noexcept -> Option<Self> {
        return rstd::bit_cast<Option<Self>>(n);
    }

    constexpr static auto make_unchecked(T n) -> Self {
        if (auto opt = make(n)) {
            return *opt;
        } else {
            unreachable();
        }
    }

    constexpr auto get() const noexcept -> T {
        static_assert(sizeof(T) == sizeof(Self));
        return rstd::bit_cast<T>(*this);
    }
    friend constexpr bool operator==(NonZero<T> a, NonZero<T> b) noexcept { return a.val == b.val; }
};

} // namespace rstd::num::nonzero

ImplNonZero(NonZeroI8Inner, i8);
ImplNonZero(NonZeroI16Inner, i16);
ImplNonZero(NonZeroI32Inner, i32);
ImplNonZero(NonZeroI64Inner, i64);

ImplNonZero(NonZeroU8Inner, u8);
ImplNonZero(NonZeroU16Inner, u16);
ImplNonZero(NonZeroU32Inner, u32);
ImplNonZero(NonZeroU64Inner, u64);

namespace rstd
{
static_assert(sizeof(rstd::num::nonzero::NonZero<u64>) == sizeof(u64));
static_assert(sizeof(rstd::option::Option<rstd::num::nonzero::NonZero<u64>>) == sizeof(u64));
} // namespace rstd

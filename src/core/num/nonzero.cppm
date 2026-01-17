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

export namespace rstd::num::nonzero
{

/// A value that is known not to equal zero.
///
/// This enables some memory layout optimization.
/// For example, `Option<NonZero<u32>>` is the same size as `u32`:
template<typename T>
struct NonZero {
    static_assert(false);
};
} // namespace rstd::num::nonzero

namespace rstd::option::detail
{

template<typename T>
struct option_store<num::nonzero::NonZero<T>> {
    constexpr auto is_some() const noexcept -> bool {
        for (usize i = 0; i < sizeof(T); i++) {
            if (m_storage[i] != rstd::byte(0)) {
                return true;
            }
        }
        return false;
    }

protected:
    using union_value_t       = num::nonzero::NonZero<T>;
    using union_const_value_t = num::nonzero::NonZero<T> const;

    constexpr auto _ptr() const noexcept {
        return reinterpret_cast<union_const_value_t*>(m_storage);
    }
    constexpr auto _ptr() noexcept { return reinterpret_cast<union_value_t*>(m_storage); }
    template<typename V>
    constexpr void _construct_val(V&& val) {
        rstd::construct_at(_ptr(), rstd::addressof(val));
    }
    template<typename V>
    constexpr void _assign_val(V&& val) {
        rstd::construct_at(_ptr(), rstd::addressof(val));
    }
    constexpr void _assign_none() {
        if (is_some()) {
            m_storage = {};
        }
    }

private:
    alignas(T) rstd::byte m_storage[sizeof(T)] {};
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

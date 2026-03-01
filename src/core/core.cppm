export module rstd.core:core;
export import :ptr.metadata;
export import :ptr.ptr;

namespace rstd
{

export template<typename T>
using param_t = mtp::conditional_t<mtp::is_trivially_copy_constructible_v<T>, T, T&&>;
export template<typename T>
using param_ref_t = mtp::conditional_t<mtp::is_trivially_copy_constructible_v<T>, T, T&>;

export template<typename T>
[[nodiscard, gnu::always_inline]] inline param_t<T> param_forward(param_ref_t<T> t) {
    if constexpr (mtp::is_trivially_copy_constructible_v<T>) {
        return t;
    } else {
        return static_cast<T&&>(t);
    }
}

static_assert(mtp::is_trivially_copy_constructible_v<i32>);
static_assert(mtp::is_trivially_copy_constructible_v<i32&>);

export template<typename To, typename From>
struct AsCast {
    static_assert(false);
};

export template<typename To, typename From>
    requires requires() { typename AsCast<To, mtp::remove_reference_t<From>>; }
[[gnu::always_inline]] inline constexpr auto as_cast(From&& from) noexcept -> To {
    return AsCast<To, mtp::remove_reference_t<From>>::cast(rstd::forward<From>(from));
}

template<typename T>
struct AsCast<mut_ptr<T>, ptr<T>> {
    static constexpr auto cast(auto&& from) noexcept -> mut_ptr<T> {
        auto raw = from.as_raw_ptr();
        using val_t =
            mtp::remove_const_t<mtp::remove_extent_t<mtp::remove_pointer_t<decltype(raw)>>>;
        return rstd::from_raw_parts_override<mut_ptr<T>>(&from, const_cast<val_t*>(raw));
    }
};

template<typename To>
struct AsCast<To*, u8*> {
    static constexpr auto cast(u8* from) noexcept -> To* { return reinterpret_cast<To*>(from); }
};
template<typename To>
struct AsCast<To*, u8 const*> {
    static constexpr auto cast(u8 const* from) noexcept -> To* {
        return reinterpret_cast<To*>(from);
    }
};

template<typename From>
struct AsCast<u8*, From*> {
    static constexpr auto cast(u8* from) noexcept -> u8* { return reinterpret_cast<u8*>(from); }
};
template<typename From>
struct AsCast<u8 const*, From*> {
    static constexpr auto cast(From* from) noexcept -> u8 const* {
        return reinterpret_cast<u8 const*>(from);
    }
};

} // namespace rstd

export module rstd.core:core;
export import :ptr.metadata;
export import :ptr.ptr;

namespace rstd
{

export template<typename T>
using param_t = mtp::cond<mtp::triv_copy<T>, T, T&&>;
export template<typename T>
using param_ref_t = mtp::cond<mtp::triv_copy<T>, T, T&>;

export template<typename T>
[[nodiscard, gnu::always_inline]] inline param_t<T> param_forward(param_ref_t<T> t) {
    if constexpr (mtp::triv_copy<T>) {
        return t;
    } else {
        return static_cast<T&&>(t);
    }
}

static_assert(mtp::triv_copy<i32>);
static_assert(mtp::triv_copy<i32&>);

export template<typename To, typename From>
struct AsCast {
    static_assert(false);
};

export template<typename To, typename From>
    requires requires() { typename AsCast<To, mtp::rm_ref<From>>; }
[[gnu::always_inline]] inline constexpr auto as_cast(From&& from) noexcept -> To {
    return AsCast<To, mtp::rm_ref<From>>::cast(rstd::forward<From>(from));
}

template<typename T>
struct AsCast<mut_ptr<T>, ptr<T>> {
    static constexpr auto cast(auto&& from) noexcept -> mut_ptr<T> {
        auto raw = from.as_raw_ptr();
        using val_t =
            mtp::rm_const<mtp::rm_ext<mtp::rm_ptr<decltype(raw)>>>;
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

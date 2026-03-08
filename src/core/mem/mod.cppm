export module rstd.core:mem;

export import :mem.manually_drop;
export import :mem.maybe_uninit;
export import :mem.drop_guard;

namespace rstd::mem
{
export {
    using drop_guard::DropGuard;
    using manually_drop::ManuallyDrop;
    using maybe_uninit::MaybeUninit;
}

export template<typename Dst, typename Src>
constexpr auto transmute(Src src) noexcept -> Dst {
    static_assert(sizeof(Dst) == sizeof(Src));
    return __builtin_bit_cast(Dst, src);
}

export constexpr auto memcmp(voidp src, voidp dst, usize len) noexcept -> bool {
    return __builtin_memcmp(src, dst, len);
}

export template<typename T>
constexpr auto all(T const& src, u8 val) noexcept -> bool {
    u8 dst[sizeof(T)] {};
    return __builtin_memcmp(addressof(src), dst, sizeof(T)) == 0;
}

export template<mtp::triv_copy T>
constexpr void fill(T& src, u8 val) noexcept {
#ifdef __clang__
    __builtin_memset(addressof(src), val, sizeof(T));
#else
    if constexpr (mtp::triv<T>) {
        __builtin_memset(addressof(src), val, sizeof(T));
    } else {
        auto p = reinterpret_cast<u8*>(addressof(src));
        for (usize i = 0; i < sizeof(T); ++i) p[i] = val;
    }
#endif
}

export template<typename T>
constexpr auto zeroed() noexcept -> T {
    return {};
}

} // namespace rstd::mem
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

/// Reinterprets the bits of `Src` as type `Dst`, analogous to Rust's `transmute`.
/// \tparam Dst The destination type.
/// \tparam Src The source type; must have the same size as `Dst`.
/// \param src The value to transmute.
/// \return The reinterpreted value.
export template<typename Dst, typename Src>
constexpr auto transmute(Src src) noexcept -> Dst {
    static_assert(sizeof(Dst) == sizeof(Src));
    return __builtin_bit_cast(Dst, src);
}

/// Compares `len` bytes of memory at `v1` and `v2`.
/// \param v1 First memory region.
/// \param v2 Second memory region.
/// \param len Number of bytes to compare.
/// \return `true` if the regions differ, `false` if equal.
export constexpr auto memcmp(const_voidp v1, const_voidp v2, usize len) noexcept -> bool {
    return __builtin_memcmp(v1, v2, len);
}

/// Fills `len` bytes of memory at `src` with `val`.
/// \param src Pointer to the memory region.
/// \param val The byte value to fill with.
/// \param len Number of bytes to set.
/// \return The destination pointer `src`.
export auto memset(voidp src, u8 val, usize len) noexcept -> voidp {
    return __builtin_memset(src, val, len);
}

/// Copies `len` bytes from `src` to `dst`; regions must not overlap.
/// \param dst Destination pointer.
/// \param src Source pointer.
/// \param len Number of bytes to copy.
/// \return The destination pointer `dst`.
export auto memcpy(voidp dst, const_voidp src, usize len) noexcept -> voidp {
    return __builtin_memcpy(dst, src, len);
}

/// Checks whether all bytes of `src` are equal to `val`.
/// \tparam T The type of the value to check.
/// \param src The value to inspect.
/// \param val The byte value to compare against.
/// \return `true` if every byte matches, `false` otherwise.
export template<typename T>
constexpr auto all(T const& src, u8 val) noexcept -> bool {
    u8 dst[sizeof(T)] {};
    return __builtin_memcmp(addressof(src), dst, sizeof(T)) == 0;
}

/// Fills all bytes of a trivially-copyable value with the given byte.
/// \tparam T A trivially copyable type.
/// \param src The value to fill.
/// \param val The byte value to set.
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

/// Returns a zero-initialized value of type `T`.
/// \tparam T The type to create.
/// \return A value-initialized (zeroed) instance of `T`.
export template<typename T>
constexpr auto zeroed() noexcept -> T {
    return {};
}

} // namespace rstd::mem
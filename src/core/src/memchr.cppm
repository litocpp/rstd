export module rstd.core:memchr;
import :num.types;
import :simd;
export import :option;

namespace rstd::memchr
{

/// Searches for the first occurrence of a byte in a slice.
/// \param needle The byte value to search for.
/// \param haystack The byte slice to search within.
/// \return The index of the first match, or `None` if not found.
export inline auto memchr(u8 needle, slice<u8> haystack) noexcept -> Option<usize> {
    using V             = simd::Simd<u8, 16>;
    auto         length = haystack.len().to_primitive();
    rstd::size_t i      = 0;
    auto         target = V::splat(needle);
    for (; length - i >= 16; i += 16) {
        auto block = slice<u8>::from_raw_parts(haystack.as_raw_ptr() + i, usize(16));
        auto first = V::from_slice(block).simd_eq(target).first_set();
        if (first.is_some()) return Some(usize(i) + first.unwrap_unchecked());
    }
    for (; i != length; ++i) {
        if (haystack[usize(i)] == needle) {
            return Some(usize(i));
        }
    }
    return None();
}

} // namespace rstd::memchr

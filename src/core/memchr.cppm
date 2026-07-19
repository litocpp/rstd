export module rstd.core:memchr;
import :num.types;
export import :option;

namespace rstd::memchr
{

/// Searches for the first occurrence of a byte in a slice.
/// \param needle The byte value to search for.
/// \param haystack The byte slice to search within.
/// \return The index of the first match, or `None` if not found.
export auto memchr(u8 needle, slice<u8> haystack) noexcept -> Option<usize> {
    for (rstd::size_t i = 0; i != haystack.len().to_primitive(); ++i) {
        if (haystack[usize(i)] == needle) {
            return Some(usize(i));
        }
    }
    return None();
}

} // namespace rstd::memchr

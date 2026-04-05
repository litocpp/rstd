export module rstd.core:memchr;
export import :option;

namespace rstd::memchr
{

/// Searches for the first occurrence of a byte in a slice.
/// \param needle The byte value to search for.
/// \param haystack The byte slice to search within.
/// \return The index of the first match, or `None` if not found.
export auto memchr(u8 needle, slice<u8> haystack) noexcept -> Option<usize> {
    for (usize i = 0; i != haystack.len(); ++i) {
        if (haystack[i] == needle) {
            return Some(i);
        }
    }
    return None();
}

} // namespace rstd::memchr
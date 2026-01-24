export module rstd.core:memchr;
export import :option;

namespace rstd::memchr
{

export auto memchr(u8 needle, slice<const u8> haystack) noexcept -> Option<usize> {
    for (usize i = 0; i != haystack.len(); ++i) {
        if (haystack[i] == needle) {
            return Some(i);
        }
    }
    return None();
}

} // namespace rstd::memchr
module;
#include <rstd/macro.hpp>

export module rstd.core:alloc.layout;
export import :num;
export import :cmp;
export import :option;
export import :assert;
export import :ptr.ptr;

namespace rstd::alloc
{

export struct Layout {
    usize size_;
    usize align_;

    constexpr Layout(usize size, usize align) : size_(size), align_(align) {
        debug_assert(align > 0 && (align & (align - 1)) == 0, "alignment must be a power of two");
    }

    static constexpr auto from_size_align(usize size, usize align) -> Option<Layout> {
        if (align == 0 || (align & (align - 1)) != 0) return None();
        if (size > (usize(-1) - (align - 1))) return None();
        return Some(Layout(size, align));
    }

    static constexpr auto from_size_align_unchecked(usize size, usize align) -> Layout {
        return Layout(size, align);
    }

    template<typename T>
    static constexpr auto new_() -> Layout {
        return Layout(sizeof(T), alignof(T));
    }

    template<typename T>
    static constexpr auto array(usize n) -> Option<Layout> {
        if (n > 0 && sizeof(T) > usize(-1) / n) return None();
        return from_size_align(n * sizeof(T), alignof(T));
    }

    constexpr usize size() const { return size_; }
    constexpr usize align() const { return align_; }

    constexpr auto dangling() const noexcept -> mut_ptr<u8> {
        return mut_ptr<u8>::from_raw_parts(reinterpret_cast<u8*>(align_));
    }
};

} // namespace rstd::alloc

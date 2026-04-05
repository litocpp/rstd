module;
#include <rstd/macro.hpp>

export module rstd.core:alloc.layout;
export import :num;
export import :cmp;
export import :option;
export import :ptr.ptr;
export import :mem;

namespace rstd::alloc
{

/// Describes the memory layout of a type: its size and alignment.
export struct Layout {
    usize size;
    usize align;

    static constexpr auto from_size_align(usize size, usize align) -> Option<Layout> {
        debug_assert(align > 0 && (align & (align - 1)) == 0, "alignment must be a power of two");
        if (align == 0 || (align & (align - 1)) != 0) return None();
        if (size > (usize(-1) - (align - 1))) return None();
        return Some(Layout { .size = size, .align = align });
    }

    static constexpr auto from_size_align_unchecked(usize size, usize align) -> Layout {
        return Layout { .size = size, .align = align };
    }

    template<typename T>
    static constexpr auto make() -> Layout {
        return Layout { .size = sizeof(T), .align = alignof(T) };
    }

    template<typename T>
    static constexpr auto array(usize n) -> Option<Layout> {
        if (n > 0 && sizeof(T) > usize(-1) / n) return None();
        return from_size_align(n * sizeof(T), alignof(T));
    }

    constexpr auto cpp_layout() const noexcept {
        return Layout { .size  = size,
                        .align = align < __STDCPP_DEFAULT_NEW_ALIGNMENT__
                                     ? __STDCPP_DEFAULT_NEW_ALIGNMENT__
                                     : align };
    }

    constexpr auto dangling() const noexcept -> mut_ptr<u8> {
        return mut_ptr<u8>::from_raw_parts(mem::transmute<u8*>(align));
    }
};

} // namespace rstd::alloc

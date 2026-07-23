module;
#include <rstd/macro.hpp>

export module rstd.core:alloc.layout;
import :num.types;
import :num.integer_methods;
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
        debug_assert(align.is_power_of_two(), "alignment must be a power of two");
        if (! align.is_power_of_two()) return None();
        auto const mask = align - usize(1);
        if (size.checked_add(mask).is_none()) return None();
        return Some(Layout { .size = size, .align = align });
    }

    static constexpr auto from_size_align_unchecked(usize size, usize align) -> Layout {
        return Layout { .size = size, .align = align };
    }

    template<typename T>
    static constexpr auto make() -> Layout {
        using Storage = typename mut_ptr<T>::storage_type;
        return Layout { .size = usize(sizeof(Storage)), .align = usize(alignof(Storage)) };
    }

    template<typename T>
    static constexpr auto array(usize n) -> Option<Layout> {
        using Storage = typename mut_ptr<T>::storage_type;
        auto size      = n.checked_mul(usize(sizeof(Storage)));
        if (size.is_none()) return None();
        return from_size_align(rstd::move(size).unwrap_unchecked(), usize(alignof(Storage)));
    }

    template<typename T>
    static auto for_value(ptr<T> pointer) noexcept -> Layout {
        if constexpr (mtp::DSTArray<T>) {
            return array<mtp::rm_ext<T>>(pointer.len()).unwrap();
        } else if constexpr (mtp::DST<T>) {
            auto const* metadata = pointer.metadata();
            return from_size_align_unchecked(metadata->size, metadata->align);
        } else {
            return make<T>();
        }
    }

    constexpr auto extend(Layout next, usize& offset) const -> Option<Layout> {
        auto const mask    = next.align - usize(1);
        usize      padding = (next.align - (size & mask)) & mask;
        auto       padded  = size.checked_add(padding);
        if (padded.is_none()) return None();
        offset             = rstd::move(padded).unwrap_unchecked();
        auto combined_size = offset.checked_add(next.size);
        if (combined_size.is_none()) return None();
        usize combined_align = align > next.align ? align : next.align;
        return from_size_align(rstd::move(combined_size).unwrap_unchecked(), combined_align);
    }

    constexpr auto pad_to_align() const noexcept -> Layout {
        auto const mask        = align - usize(1);
        usize      padded_size = (size + mask) & ~mask;
        return Layout { .size = padded_size, .align = align };
    }

    constexpr auto cpp_layout() const noexcept {
        auto const default_alignment = usize(__STDCPP_DEFAULT_NEW_ALIGNMENT__);
        return Layout { .size  = size,
                        .align = align < default_alignment ? default_alignment : align };
    }

    auto dangling() const noexcept -> void* {
        return reinterpret_cast<void*>(align.to_primitive());
    }
};

} // namespace rstd::alloc

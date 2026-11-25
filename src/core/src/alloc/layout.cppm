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
export class Layout {
    usize size_ {};
    usize align_ { 1 };

    [[gnu::always_inline]]
    constexpr Layout(usize size, usize align) noexcept
        : size_(size), align_(align) {}

    [[gnu::always_inline]]
    static constexpr auto valid_alignment(size_t align) noexcept -> bool {
        return align != 0 && (align & (align - 1)) == 0;
    }

    [[gnu::always_inline]]
    static constexpr auto max_size_for_align(size_t align) noexcept -> size_t {
        return static_cast<size_t>(isize::MAX.to_primitive()) + 1 - align;
    }

public:
    constexpr Layout() noexcept = default;

    [[gnu::always_inline]]
    constexpr auto size() const noexcept -> usize {
        return size_;
    }
    [[gnu::always_inline]]
    constexpr auto align() const noexcept -> usize {
        return align_;
    }

    static constexpr auto from_size_align(usize size, usize align) -> Option<Layout> {
        auto alignment = align.to_primitive();
        if (! valid_alignment(alignment) || size.to_primitive() > max_size_for_align(alignment))
            return None();
        return Some(Layout(size, align));
    }

    // The caller must provide a power-of-two alignment and a size whose padding fits isize.
    static constexpr auto from_size_align_unchecked(usize size, usize align) -> Layout {
        debug_assert(valid_alignment(align.to_primitive()) &&
                     size.to_primitive() <= max_size_for_align(align.to_primitive()));
        return Layout(size, align);
    }

    template<typename T>
    [[gnu::always_inline]]
    static constexpr auto make() -> Layout {
        using Storage = typename mut_ptr<T>::storage_type;
        static_assert(sizeof(Storage) <= max_size_for_align(alignof(Storage)));
        return Layout(usize(sizeof(Storage)), usize(alignof(Storage)));
    }

    template<typename T>
    static constexpr auto array(usize n) -> Option<Layout> {
        using Storage            = typename mut_ptr<T>::storage_type;
        constexpr auto max_count = max_size_for_align(alignof(Storage)) / sizeof(Storage);
        if (n.to_primitive() > max_count) return None();
        return Some(Layout(usize(n.to_primitive() * sizeof(Storage)), usize(alignof(Storage))));
    }

    template<typename T>
    static constexpr auto array(usize n, usize alignment) -> Option<Layout> {
        using Storage = typename mut_ptr<T>::storage_type;
        auto align    = alignment.to_primitive();
        if (align == alignof(Storage)) return array<T>(n);
        if (align < alignof(Storage) || ! valid_alignment(align)) return None();
        if (n.to_primitive() > max_size_for_align(align) / sizeof(Storage)) return None();
        return Some(Layout(usize(n.to_primitive() * sizeof(Storage)), alignment));
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
        // Valid layouts bound both sums below usize::MAX; only the final isize limit remains.
        auto mask           = next.align_.to_primitive() - 1;
        auto padded         = (size_.to_primitive() + mask) & ~mask;
        auto combined_align = align_ > next.align_ ? align_ : next.align_;
        auto combined_size  = padded + next.size_.to_primitive();
        if (combined_size > max_size_for_align(combined_align.to_primitive())) return None();
        offset = usize(padded);
        return Some(Layout(usize(combined_size), combined_align));
    }

    constexpr auto pad_to_align() const noexcept -> Layout {
        auto mask        = align_.to_primitive() - 1;
        auto padded_size = (size_.to_primitive() + mask) & ~mask;
        return Layout(usize(padded_size), align_);
    }

    constexpr auto align_to(usize alignment) const -> Option<Layout> {
        if (! valid_alignment(alignment.to_primitive())) return None();
        auto combined_align = align_ > alignment ? align_ : alignment;
        if (size_.to_primitive() > max_size_for_align(combined_align.to_primitive())) return None();
        return Some(Layout(size_, combined_align));
    }

    constexpr auto cpp_layout() const -> Option<Layout> {
        return align_to(usize(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
    }

    /// Returns an aligned sentinel without referring to allocated storage.
    auto dangling() const noexcept -> void* {
        [[unsafe]] { return reinterpret_cast<void*>(align_.to_primitive()); }
    }
};

} // namespace rstd::alloc

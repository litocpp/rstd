export module rstd.core:alloc.global;
export import :alloc.layout;

namespace rstd::alloc
{

/// A low-level trait for a global memory allocator.
export struct GlobalAlloc {
    template<typename Self, typename = void>
    struct Api {
        using Trait = GlobalAlloc;

        auto alloc(Layout layout) const noexcept -> mut_ptr<u8> {
            return trait_call<0>(this, layout);
        }

        void dealloc(mut_ptr<u8> ptr, Layout layout) const noexcept {
            trait_call<1>(this, ptr, layout);
        }

        auto realloc(mut_ptr<u8> ptr, Layout layout, usize new_size) const noexcept -> mut_ptr<u8> {
            return trait_call<2>(this, ptr, layout, new_size);
        }

        auto alloc_zeroed(Layout layout) const noexcept -> mut_ptr<u8> {
            return trait_call<3>(this, layout);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::alloc, &T::dealloc, &T::realloc, &T::alloc_zeroed>;
};

// Default implementations for GlobalAlloc methods
export namespace global_alloc_defaults
{
template<typename T>
auto realloc(const T& self, mut_ptr<u8> ptr, Layout layout, usize new_size) noexcept
    -> mut_ptr<u8> {
    auto new_layout = Layout::from_size_align(new_size, layout.align()).unwrap();
    auto new_ptr    = as<GlobalAlloc>(self).alloc(new_layout);
    if (new_ptr != nullptr) {
        usize copy_size = layout.size() < new_size ? layout.size() : new_size;
        rstd::memcpy(new_ptr.as_raw_ptr(), ptr.as_raw_ptr(), copy_size);
        as<GlobalAlloc>(self).dealloc(ptr, layout);
    }
    return new_ptr;
}

template<typename T>
auto alloc_zeroed(const T& self, Layout layout) noexcept -> mut_ptr<u8> {
    auto ptr = as<GlobalAlloc>(self).alloc(layout);
    if (ptr != nullptr) {
        rstd::memset(ptr.as_raw_ptr(), 0, layout.size());
    }
    return ptr;
}
} // namespace global_alloc_defaults

} // namespace rstd::alloc
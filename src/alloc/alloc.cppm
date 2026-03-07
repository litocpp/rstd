module;
#include <rstd/macro.hpp>

export module rstd.alloc:alloc;
export import rstd.core;

using namespace rstd::literal;
using rstd::mut_ptr;
using rstd::TraitFuncs;
using rstd::alloc::Layout;
using rstd::ptr_::non_null::NonNull;
using rstd::result::Result;

// External symbols to be implemented by the runtime
extern "C" {
void* __rstd_alloc(usize size, usize align);
void  __rstd_dealloc(void* ptr, usize size, usize align);
void* __rstd_realloc(void* ptr, usize old_size, usize align, usize new_size);
void* __rstd_alloc_zeroed(usize size, usize align);
}

namespace alloc
{

// Error type for allocation failures
export struct AllocError {};



/// A high-level trait for an allocator.
export struct Allocator {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Allocator;

        auto allocate(Layout layout) const -> Result<NonNull<u8[]>, AllocError> {
            return trait_call<0>(this, layout);
        }

        auto allocate_zeroed(Layout layout) const -> Result<NonNull<u8[]>, AllocError> {
            return trait_call<1>(this, layout);
        }

        void deallocate(NonNull<u8> ptr, Layout layout) const noexcept {
            trait_call<2>(this, ptr, layout);
        }

        auto grow(NonNull<u8> ptr, Layout old_layout, Layout new_layout) const
            -> Result<NonNull<u8[]>, AllocError> {
            return trait_call<3>(this, ptr, old_layout, new_layout);
        }

        auto grow_zeroed(NonNull<u8> ptr, Layout old_layout, Layout new_layout) const
            -> Result<NonNull<u8[]>, AllocError> {
            return trait_call<4>(this, ptr, old_layout, new_layout);
        }

        auto shrink(NonNull<u8> ptr, Layout old_layout, Layout new_layout) const
            -> Result<NonNull<u8[]>, AllocError> {
            return trait_call<5>(this, ptr, old_layout, new_layout);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::allocate, &T::allocate_zeroed, &T::deallocate, &T::grow,
                             &T::grow_zeroed, &T::shrink>;
};

// Default implementations for Allocator methods
export namespace allocator_defaults
{
template<typename T>
auto allocate_zeroed(const T& self, Layout layout) -> Result<NonNull<u8[]>, AllocError> {
    auto res = as<Allocator>(self).allocate(layout);
    if (res.is_ok()) {
        auto ptr = res.unwrap_unchecked();
        rstd::memset(ptr.as_mut_ptr().as_raw_ptr(), 0, layout.size());
    }
    return res;
}

template<typename T>
auto grow(const T& self, NonNull<u8> ptr, Layout old_layout, Layout new_layout)
    -> Result<NonNull<u8[]>, AllocError> {
    debug_assert(new_layout.size() >= old_layout.size());
    if (old_layout.size() == 0) {
        return rstd::as<Allocator>(self).allocate(new_layout);
    }

    auto new_res = rstd::as<Allocator>(self).allocate(new_layout);
    if (new_res.is_ok()) {
        auto new_ptr = new_res.unwrap_unchecked();
        rstd::memcpy(
            new_ptr.as_mut_ptr().as_raw_ptr(), ptr.as_ptr().as_raw_ptr(), old_layout.size());
        rstd::as<Allocator>(self).deallocate(ptr, old_layout);
    }
    return new_res;
}
} // namespace allocator_defaults

/// The global memory allocator.
export struct Global {};

export constexpr Global GLOBAL {};

/**
 * Functions that use the global allocator registered with __rstd_alloc etc.
 */

export auto alloc(Layout layout) noexcept -> mut_ptr<u8> {
    return mut_ptr<u8>::from_raw_parts(
        static_cast<u8*>(__rstd_alloc(layout.size(), layout.align())));
}

export void dealloc(mut_ptr<u8> ptr, Layout layout) noexcept {
    __rstd_dealloc(ptr.as_raw_ptr(), layout.size(), layout.align());
}

export auto realloc(mut_ptr<u8> ptr, Layout layout, usize new_size) noexcept -> mut_ptr<u8> {
    return mut_ptr<u8>::from_raw_parts(static_cast<u8*>(
        __rstd_realloc(ptr.as_raw_ptr(), layout.size(), layout.align(), new_size)));
}

export auto alloc_zeroed(Layout layout) noexcept -> mut_ptr<u8> {
    return mut_ptr<u8>::from_raw_parts(
        static_cast<u8*>(__rstd_alloc_zeroed(layout.size(), layout.align())));
}

export [[gnu::cold]]
void handle_alloc_error(Layout layout) {
    rstd::panic("memory allocation of {} bytes failed", layout.size());
}

} // namespace alloc

namespace alloc_ = alloc;

template<>
struct rstd::Impl<alloc_::Allocator, alloc_::Global> : ImplBase<alloc_::Global> {
    auto allocate(Layout layout) const -> Result<NonNull<u8[]>, alloc_::AllocError> {
        if (layout.size() == 0) {
            return Ok(NonNull<u8[]>::make_unchecked(layout.dangling().template cast_array<u8>()));
        }
        auto p = alloc_::alloc(layout);
        if (p == nullptr) return Err(alloc_::AllocError {});
        return Ok(NonNull<u8[]>::make_unchecked(p.template cast_array<u8>(layout.size())));
    }

    auto allocate_zeroed(Layout layout) const -> Result<NonNull<u8[]>, alloc_::AllocError> {
        if (layout.size() == 0) {
            return Ok(NonNull<u8[]>::make_unchecked(layout.dangling().template cast_array<u8>()));
        }
        auto p = alloc_::alloc_zeroed(layout);
        if (p == nullptr) return Err(alloc_::AllocError {});
        return Ok(NonNull<u8[]>::make_unchecked(p.template cast_array<u8>(layout.size())));
    }

    void deallocate(NonNull<u8> ptr, Layout layout) const noexcept {
        if (layout.size() != 0) {
            alloc_::dealloc(ptr.as_mut_ptr(), layout);
        }
    }

    auto grow(NonNull<u8> ptr, Layout old_layout, Layout new_layout) const
        -> Result<NonNull<u8[]>, alloc_::AllocError> {
        return alloc_::allocator_defaults::grow(this->self(), ptr, old_layout, new_layout);
    }

    auto grow_zeroed(NonNull<u8> ptr, Layout old_layout, Layout new_layout) const
        -> Result<NonNull<u8[]>, alloc_::AllocError> {
        // ...
        return Err(alloc_::AllocError {});
    }

    auto shrink(NonNull<u8> ptr, Layout old_layout, Layout new_layout) const
        -> Result<NonNull<u8[]>, alloc_::AllocError> {
        // ...
        return Err(alloc_::AllocError {});
    }
};

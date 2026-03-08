module;
#include <rstd/macro.hpp>
export module rstd.core:alloc;
export import :alloc.global;
export import :alloc.layout;
export import :ptr.non_null;
export import :result;

using rstd::ptr_::non_null::NonNull;
using rstd::result::Result;
namespace rstd::alloc
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
} // namespace rstd::alloc

namespace rstd
{
using alloc::Allocator;
using alloc::AllocError;
using alloc::Layout;

template<typename Self, auto P>
struct Impl<Allocator, default_tag<Self, P>> : ImplBase<default_tag<Self, P>> {
    auto allocate_zeroed(Layout layout) -> Result<NonNull<u8[]>, AllocError> {
        auto res = as<Allocator>(this->self()).allocate(layout);
        if (res.is_ok()) {
            auto ptr = res.unwrap_unchecked();
            rstd::memset(ptr.as_mut_ptr().as_raw_ptr(), 0, layout.size);
        }
        return res;
    }

    auto grow(NonNull<u8> ptr, Layout old_layout, Layout new_layout)
        -> Result<NonNull<u8[]>, AllocError> {
        debug_assert(new_layout.size >= old_layout.size);
        if (old_layout.size == 0) {
            return rstd::as<Allocator>(this->self()).allocate(new_layout);
        }

        auto new_res = rstd::as<Allocator>(this->self()).allocate(new_layout);
        if (new_res.is_ok()) {
            auto new_ptr = new_res.unwrap_unchecked();
            rstd::memcpy(
                new_ptr.as_mut_ptr().as_raw_ptr(), ptr.as_ptr().as_raw_ptr(), old_layout.size);
            rstd::as<Allocator>(this->self()).deallocate(ptr, old_layout);
        }
        return new_res;
    }

    auto grow_zeroed(NonNull<u8> ptr, Layout old_layout, Layout new_layout) const
        -> Result<NonNull<u8[]>, AllocError> {
        debug_assert(new_layout.size >= old_layout.size,
                     "`new_layout.size` must be greater than or equal to `old_layout.size`");

        auto new_ptr = rstd::as<Allocator>(this->self()).allocate_zeroed(new_layout);

        return new_ptr;
    }

    auto shrink(NonNull<u8> ptr, Layout old_layout, Layout new_layout) const
        -> Result<NonNull<u8[]>, AllocError> {
        debug_assert(new_layout.size <= old_layout.size,
                     "`new_layout.size` must be smaller than or equal to `old_layout.size`");
        auto new_ptr = rstd::as<Allocator>(this->self()).allocate(new_layout);

        // // SAFETY: because `new_layout.size()` must be lower than or equal to
        // // `old_layout.size()`, both the old and new memory allocation are valid for reads and
        // // writes for `new_layout.size()` bytes. Also, because the old allocation wasn't yet
        // // deallocated, it cannot overlap `new_ptr`. Thus, the call to `copy_nonoverlapping` is
        // // safe. The safety contract for `dealloc` must be upheld by the caller.
        // unsafe {
        //     ptr::copy_nonoverlapping(ptr.as_ptr(), new_ptr.as_mut_ptr(), new_layout.size());
        //     self.deallocate(ptr, old_layout);
        // }
        return new_ptr;
    }
};
} // namespace rstd

template<>
struct rstd::fmt::formatter<rstd::alloc::AllocError> : rstd::fmt::formatter<cppstd::string_view> {
    template<class FmtContext>
    FmtContext::iterator format(rstd::alloc::AllocError const& err, FmtContext& ctx) const {
        return rstd::fmt::formatter<cppstd::string_view>::format("AllocError", ctx);
    }
};

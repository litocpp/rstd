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

/// The error type for fallible allocation operations.
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
private:
    auto impl_() const { return Impl<Allocator, Self> { &this->self() }; }
    auto impl_() { return Impl<Allocator, Self> { &this->self() }; }

public:
    auto allocate_zeroed(Layout layout) const -> Result<NonNull<u8[]>, AllocError> {
        auto res = impl_().allocate(layout);
        if (res.is_ok()) {
            auto ptr = res.unwrap_unchecked();
            mem::memset(ptr.as_mut_ptr().as_raw_ptr(), 0, layout.size);
        }
        return res;
    }

    auto grow(NonNull<u8> ptr, Layout old_layout, Layout new_layout) const
        -> Result<NonNull<u8[]>, AllocError> {
        debug_assert(new_layout.size >= old_layout.size);
        if (old_layout.size == 0) {
            return impl_().allocate(new_layout);
        }

        auto new_res = impl_().allocate(new_layout);
        if (new_res.is_ok()) {
            auto new_ptr = new_res.unwrap_unchecked();
            mem::memcpy(
                new_ptr.as_mut_ptr().as_raw_ptr(), ptr.as_ptr().as_raw_ptr(), old_layout.size);
            impl_().deallocate(ptr, old_layout);
        }
        return new_res;
    }

    auto grow_zeroed(NonNull<u8> ptr, Layout old_layout, Layout new_layout) const
        -> Result<NonNull<u8[]>, AllocError> {
        debug_assert(new_layout.size >= old_layout.size,
                     "`new_layout.size` must be greater than or equal to `old_layout.size`");
        return impl_().allocate_zeroed(new_layout);
    }

    auto shrink(NonNull<u8> ptr, Layout old_layout, Layout new_layout) const
        -> Result<NonNull<u8[]>, AllocError> {
        debug_assert(new_layout.size <= old_layout.size,
                     "`new_layout.size` must be smaller than or equal to `old_layout.size`");
        return impl_().allocate(new_layout);
    }
};
} // namespace rstd::alloc


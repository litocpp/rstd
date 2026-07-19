module;
#include <rstd/macro.hpp>
export module rstd.core:alloc;
import :num.types;
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

/// A non-null raw allocation. The storage does not contain `u8` objects until an owner constructs
/// them explicitly.
export struct Allocation {
    void* pointer;
    usize size;

    template<typename T>
    auto as_mut_ptr() const noexcept -> mut_ptr<T> {
        return mut_ptr<T>::from_raw_parts(static_cast<T*>(pointer));
    }

    template<typename T>
    auto as_mut_slice(usize length) const noexcept -> mut_ptr<T[]> {
        return mut_ptr<T[]>::from_raw_parts(static_cast<T*>(pointer), length);
    }
};

/// A high-level trait for an allocator.
export struct Allocator {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Allocator;

        auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
            return trait_call<0>(this, layout);
        }

        auto allocate_zeroed(Layout layout) const -> Result<Allocation, AllocError> {
            return trait_call<1>(this, layout);
        }

        void deallocate(void* ptr, Layout layout) const noexcept {
            trait_call<2>(this, ptr, layout);
        }

        auto grow(void* ptr, Layout old_layout, Layout new_layout) const
            -> Result<Allocation, AllocError> {
            return trait_call<3>(this, ptr, old_layout, new_layout);
        }

        auto grow_zeroed(void* ptr, Layout old_layout, Layout new_layout) const
            -> Result<Allocation, AllocError> {
            return trait_call<4>(this, ptr, old_layout, new_layout);
        }

        auto shrink(void* ptr, Layout old_layout, Layout new_layout) const
            -> Result<Allocation, AllocError> {
            return trait_call<5>(this, ptr, old_layout, new_layout);
        }
    };

    template<typename T>
    using Funcs = TraitFuncs<&T::allocate,
                             &T::allocate_zeroed,
                             &T::deallocate,
                             &T::grow,
                             &T::grow_zeroed,
                             &T::shrink>;
};
} // namespace rstd::alloc

namespace rstd
{
using alloc::Allocator;
using alloc::AllocError;
using alloc::Allocation;
using alloc::Layout;

template<typename Tag>
    requires mtp::trait_default_tag<Tag>
struct Impl<Allocator, Tag> : ImplBase<Tag> {
private:
    decltype(auto) allocator() const { return as<Allocator>(this->self()); }

public:
    auto allocate_zeroed(Layout layout) const -> Result<Allocation, AllocError> {
        auto res = allocator().allocate(layout);
        if (res.is_ok()) {
            auto allocation = res.unwrap_unchecked();
            mem::memset(allocation.pointer, u8(), layout.size);
        }
        return res;
    }

    auto grow(void* ptr, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size >= old_layout.size);
        if (old_layout.size == usize()) {
            return allocator().allocate(new_layout);
        }

        auto new_res = allocator().allocate(new_layout);
        if (new_res.is_ok()) {
            auto new_ptr = new_res.unwrap_unchecked();
            mem::memcpy(new_ptr.pointer, ptr, old_layout.size);
            allocator().deallocate(ptr, old_layout);
        }
        return new_res;
    }

    auto grow_zeroed(void* ptr, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size >= old_layout.size,
                     "`new_layout.size` must be greater than or equal to `old_layout.size`");
        return allocator().allocate_zeroed(new_layout);
    }

    auto shrink(void* ptr, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size <= old_layout.size,
                     "`new_layout.size` must be smaller than or equal to `old_layout.size`");
        return allocator().allocate(new_layout);
    }
};
} // namespace rstd

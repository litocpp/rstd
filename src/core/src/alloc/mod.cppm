module;
#include <rstd/macro.hpp>
export module rstd.core:alloc;
import :num.types;
import :error.trait;
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
        using Storage = typename mut_ptr<T>::storage_type;
        return mut_ptr<T>::from_raw_parts(static_cast<Storage*>(pointer));
    }

    template<typename T>
    auto as_mut_slice(usize length) const noexcept -> mut_ptr<T[]> {
        using Storage = typename mut_ptr<T[]>::storage_type;
        return mut_ptr<T[]>::from_raw_parts(static_cast<Storage*>(pointer), length);
    }
};

/// Cloning this allocator preserves the allocation domain and release compatibility.
export struct AllocatorClone {};

/// Ownership may move between threads when access and final release are externally serialized.
export struct TransferableAllocator {};

/// Operations and final release may run on arbitrary threads, including concurrently.
export struct ThreadSafeAllocator {};

/// A high-level trait for CPU-accessible storage. Moving an allocator preserves its domain.
/// A successful resize invalidates the old pointer; failure leaves its allocation untouched.
/// Returned storage meets the requested alignment and has at least the requested size.
/// Release layouts match the latest successful request alignment and a size within its returned range.
/// Zero-sized layouts accept a non-null, aligned dangling pointer; no initialized payload is implied.
/// Allocator methods, moves and allocator destruction must not throw.
export struct Allocator {
    template<typename Self, typename = void>
    struct Api {
        using Trait = Allocator;

        auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
            return trait_call<0>(this, layout);
        }

        /// Initializes the full returned usable size, including excess capacity.
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

template<>
struct Impl<fmt::Display, alloc::AllocError> : ImplBase<alloc::AllocError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_raw("memory allocation failed",
                                   sizeof("memory allocation failed") - 1);
    }
};

template<>
struct Impl<fmt::Debug, alloc::AllocError> : ImplBase<alloc::AllocError> {
    auto fmt(fmt::Formatter& formatter) const -> bool {
        return formatter.write_raw("AllocError", sizeof("AllocError") - 1);
    }
};

template<>
struct Impl<error::Error, alloc::AllocError> : ImplBase<alloc::AllocError> {
    auto source() const noexcept -> Option<error::ErrorRef> { return None(); }
};

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
            mem::memset(allocation.pointer, u8(), allocation.size);
        }
        return res;
    }

    auto grow(void* ptr, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size >= old_layout.size);
        auto new_res = allocator().allocate(new_layout);
        if (new_res.is_ok()) {
            auto new_ptr = new_res.unwrap_unchecked();
            if (old_layout.size != usize()) mem::memcpy(new_ptr.pointer, ptr, old_layout.size);
            allocator().deallocate(ptr, old_layout);
        }
        return new_res;
    }

    auto grow_zeroed(void* ptr, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size >= old_layout.size,
                     "`new_layout.size` must be greater than or equal to `old_layout.size`");
        auto result = allocator().allocate_zeroed(new_layout);
        if (result.is_ok()) {
            if (old_layout.size != usize())
                mem::memcpy(result.unwrap_unchecked().pointer, ptr, old_layout.size);
            allocator().deallocate(ptr, old_layout);
        }
        return result;
    }

    auto shrink(void* ptr, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size <= old_layout.size,
                     "`new_layout.size` must be smaller than or equal to `old_layout.size`");
        if (new_layout.size == old_layout.size && new_layout.align == old_layout.align) {
            return Ok(Allocation { ptr, new_layout.size });
        }

        auto new_res = allocator().allocate(new_layout);
        if (new_res.is_ok()) {
            auto new_ptr = new_res.unwrap_unchecked();
            if (new_layout.size != usize()) mem::memcpy(new_ptr.pointer, ptr, new_layout.size);
            allocator().deallocate(ptr, old_layout);
        }
        return new_res;
    }
};
} // namespace rstd

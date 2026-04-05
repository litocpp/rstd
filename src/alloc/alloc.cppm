module;
#include <rstd/macro.hpp>

export module rstd.alloc:alloc;
export import rstd.core;

using namespace rstd::prelude;
using rstd::mut_ptr;
using rstd::alloc::Allocator;
using rstd::alloc::AllocError;
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

/// Allocates memory with the given layout using the global allocator.
/// \param layout The memory layout describing size and alignment requirements.
/// \return A mutable pointer to the allocated memory.
export auto alloc(Layout layout) noexcept -> mut_ptr<u8> {
    layout = layout.cpp_layout();
    return mut_ptr<u8>::from_raw_parts(static_cast<u8*>(__rstd_alloc(layout.size, layout.align)));
}

/// Deallocates memory previously allocated with the given layout.
/// \param ptr The pointer to the memory to deallocate.
/// \param layout The layout that was used to allocate the memory.
export void dealloc(mut_ptr<u8> ptr, Layout layout) noexcept {
    layout = layout.cpp_layout();
    __rstd_dealloc(ptr.as_raw_ptr(), layout.size, layout.align);
}

/// Reallocates memory to a new size, preserving existing data up to the minimum of old and new sizes.
/// \param ptr The pointer to the previously allocated memory.
/// \param layout The layout that was used for the original allocation.
/// \param new_size The desired new size in bytes.
/// \return A mutable pointer to the reallocated memory.
export auto realloc(mut_ptr<u8> ptr, Layout layout, usize new_size) noexcept -> mut_ptr<u8> {
    layout = layout.cpp_layout();
    return mut_ptr<u8>::from_raw_parts(
        static_cast<u8*>(__rstd_realloc(ptr.as_raw_ptr(), layout.size, layout.align, new_size)));
}

/// Allocates zero-initialized memory with the given layout.
/// \param layout The memory layout describing size and alignment requirements.
/// \return A mutable pointer to the zero-initialized allocated memory.
export auto alloc_zeroed(Layout layout) noexcept -> mut_ptr<u8> {
    layout = layout.cpp_layout();
    return mut_ptr<u8>::from_raw_parts(
        static_cast<u8*>(__rstd_alloc_zeroed(layout.size, layout.align)));
}

/// Aborts the process on memory allocation failure.
/// \param layout The layout of the allocation that failed.
export [[gnu::cold]]
void handle_alloc_error(Layout layout) {
    rstd::panic { "memory allocation failed" };
}

/// The global memory allocator.
export struct Global;

} // namespace alloc

namespace alloc_ = alloc;

/// Impl before Global definition — methods live here, not on Global.
/// Inherits LinkTraitDefault so default grow/shrink/grow_zeroed call back
/// through impl_() which constructs Impl directly (no trait_call).
template<>
struct rstd::Impl<rstd::alloc::Allocator, alloc_::Global>
    : LinkTraitDefault<rstd::alloc::Allocator, alloc_::Global> {
    auto allocate(Layout layout) const -> Result<NonNull<u8[]>, AllocError> {
        if (layout.size == 0) {
            return Ok(NonNull<u8[]>::make_unchecked(layout.dangling().template cast_array<u8>()));
        }
        auto p = ::alloc::alloc(layout);
        if (p == nullptr) return Err(AllocError {});
        return Ok(NonNull<u8[]>::make_unchecked(p.template cast_array<u8>(layout.size)));
    }

    auto allocate_zeroed(Layout layout) const -> Result<NonNull<u8[]>, AllocError> {
        if (layout.size == 0) {
            return Ok(NonNull<u8[]>::make_unchecked(layout.dangling().template cast_array<u8>()));
        }
        auto p = ::alloc::alloc_zeroed(layout);
        if (p == nullptr) return Err(AllocError {});
        return Ok(NonNull<u8[]>::make_unchecked(p.template cast_array<u8>(layout.size)));
    }

    void deallocate(NonNull<u8> ptr, Layout layout) const noexcept {
        if (layout.size != 0) {
            ::alloc::dealloc(ptr.as_mut_ptr(), layout.cpp_layout());
        }
    }
};

namespace alloc
{

/// The global memory allocator, implementing the `Allocator` trait.
export struct Global {};

/// The singleton instance of the global allocator.
export Global GLOBAL {};

} // namespace alloc

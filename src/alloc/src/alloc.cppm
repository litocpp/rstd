export module rstd.alloc:alloc;
export import rstd.core;

using namespace rstd::prelude;
using rstd::mut_ptr;
using rstd::alloc::Allocator;
using rstd::alloc::AllocError;
using rstd::alloc::Allocation;
using rstd::alloc::Layout;
using rstd::ptr_::non_null::NonNull;
using rstd::result::Result;

// External symbols to be implemented by the runtime
extern "C" {
void* __rstd_alloc(rstd::size_t size, rstd::size_t align);
void  __rstd_dealloc(void* ptr, rstd::size_t size, rstd::size_t align);
void* __rstd_realloc(void* ptr, rstd::size_t old_size, rstd::size_t align, rstd::size_t new_size);
void* __rstd_alloc_zeroed(rstd::size_t size, rstd::size_t align);
}

namespace alloc
{

/// Allocates memory with the given layout using the global allocator.
/// \param layout The memory layout describing size and alignment requirements.
/// \return A mutable pointer to the allocated memory.
export auto alloc(Layout layout) noexcept -> void* {
    layout = layout.cpp_layout();
    return __rstd_alloc(layout.size.to_primitive(), layout.align.to_primitive());
}

/// Deallocates memory previously allocated with the given layout.
/// \param ptr The pointer to the memory to deallocate.
/// \param layout The layout that was used to allocate the memory.
export void dealloc(void* ptr, Layout layout) noexcept {
    layout = layout.cpp_layout();
    __rstd_dealloc(ptr, layout.size.to_primitive(), layout.align.to_primitive());
}

/// Reallocates memory to a new size, preserving existing data up to the minimum of old and new sizes.
/// \param ptr The pointer to the previously allocated memory.
/// \param layout The layout that was used for the original allocation.
/// \param new_size The desired new size in bytes.
/// \return A mutable pointer to the reallocated memory.
export auto realloc(void* ptr, Layout layout, usize new_size) noexcept -> void* {
    layout = layout.cpp_layout();
    return __rstd_realloc(
        ptr, layout.size.to_primitive(), layout.align.to_primitive(), new_size.to_primitive());
}

/// Allocates zero-initialized memory with the given layout.
/// \param layout The memory layout describing size and alignment requirements.
/// \return A mutable pointer to the zero-initialized allocated memory.
export auto alloc_zeroed(Layout layout) noexcept -> void* {
    layout = layout.cpp_layout();
    return __rstd_alloc_zeroed(layout.size.to_primitive(), layout.align.to_primitive());
}

/// Aborts the process on memory allocation failure.
/// \param layout The layout of the allocation that failed.
export [[gnu::cold]]
void handle_alloc_error(Layout                layout,
                        rstd::source_location loc = rstd::source_location::current()) {
    rstd::panic_message("memory allocation failed", loc);
}

// Forward declaration of the global memory allocator.
export struct Global;

} // namespace alloc

namespace alloc_ = alloc;

// Keep this implementation before Global so the default operations dispatch
// through impl_() without constructing the trait object.
template<>
struct rstd::Impl<rstd::alloc::Allocator, alloc_::Global>
    : DefaultInImpl<rstd::alloc::Allocator, alloc_::Global> {
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        if (layout.size == usize()) return Ok(Allocation { layout.dangling(), layout.size });
        auto p = ::alloc::alloc(layout);
        if (p == nullptr) return Err(AllocError {});
        return Ok(Allocation { p, layout.size });
    }

    auto allocate_zeroed(Layout layout) const -> Result<Allocation, AllocError> {
        if (layout.size == usize()) return Ok(Allocation { layout.dangling(), layout.size });
        auto p = ::alloc::alloc_zeroed(layout);
        if (p == nullptr) return Err(AllocError {});
        return Ok(Allocation { p, layout.size });
    }

    void deallocate(void* ptr, Layout layout) const noexcept {
        if (layout.size != usize()) ::alloc::dealloc(ptr, layout.cpp_layout());
    }
};

namespace alloc
{

/// The global memory allocator, implementing the `Allocator` trait.
export struct Global {};

/// The singleton instance of the global allocator.
export Global GLOBAL {};

} // namespace alloc

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

struct RawAllocationGuard {
    void*  pointer;
    Layout layout;
    ~RawAllocationGuard() {
        if (pointer != nullptr) ::alloc::dealloc(pointer, layout);
    }
};

/// Allocates byte storage globally and constructs one T. The caller owns the returned object.
/// Construction failure releases the storage; allocation failure invokes handle_alloc_error.
/// Pair with raw_delete using the exact allocated type, not delete or delete[].
export template<typename T, typename... Args>
    requires(! rstd::mtp::is_array<T>) && rstd::mtp::same_as<T, rstd::mtp::rm_cvf<T>> &&
            requires(T* pointer, Args&&... args) {
                rstd::construct_at(pointer, rstd::forward<Args>(args)...);
            }
auto raw_new(Args&&... args) noexcept(noexcept(rstd::construct_at(static_cast<T*>(nullptr),
                                                                  rstd::forward<Args>(args)...)))
    -> T* {
    auto  layout  = Layout::from_size_align_unchecked(usize(sizeof(T)), usize(alignof(T)));
    auto* storage = ::alloc::alloc(layout);
    if (storage == nullptr) handle_alloc_error(layout);
    RawAllocationGuard guard { storage, layout };
    auto* value   = rstd::construct_at(static_cast<T*>(storage), rstd::forward<Args>(args)...);
    guard.pointer = nullptr;
    return value;
}

/// Destroys one raw_new object and releases its storage, including when destruction throws.
/// Null is accepted. The pointer must retain the exact allocated type and allocation address.
export template<typename T>
    requires(! rstd::mtp::is_array<T>) && (! rstd::mtp::is_void<T>)
void raw_delete(T* pointer) noexcept(rstd::mtp::noex_drop_v<T>) {
    if (pointer == nullptr) return;
    auto layout = Layout::from_size_align_unchecked(usize(sizeof(T)), usize(alignof(T)));
    RawAllocationGuard guard { const_cast<rstd::mtp::rm_const<T>*>(pointer), layout };
    rstd::destroy_at(pointer);
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

    // Inherited member pointers in Global's dyn vtable crash Windows Clang with reduced BMIs.
    auto grow(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        return DefaultInImpl<Allocator, alloc_::Global>::grow(pointer, old_layout, new_layout);
    }

    auto grow_zeroed(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        return DefaultInImpl<Allocator, alloc_::Global>::grow_zeroed(
            pointer, old_layout, new_layout);
    }

    auto shrink(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        return DefaultInImpl<Allocator, alloc_::Global>::shrink(pointer, old_layout, new_layout);
    }
};

namespace alloc
{

/// The global memory allocator, implementing the `Allocator` trait.
export struct Global {};

/// The singleton instance of the global allocator.
export Global GLOBAL {};

/// Borrows an allocator through its runtime trait interface.
export template<typename A>
    requires rstd::Impled<A, Allocator>
auto allocator_ref(A& allocator [[clang::lifetimebound]]) noexcept -> ref<dyn<Allocator>> {
    return dyn<Allocator>::from_ref(allocator).as_ref();
}

} // namespace alloc

template<>
struct rstd::Impl<rstd::alloc::Allocator, rstd::ref<rstd::dyn<rstd::alloc::Allocator>>>
    : rstd::ImplBase<rstd::ref<rstd::dyn<rstd::alloc::Allocator>>> {
private:
    using Self = rstd::ref<rstd::dyn<rstd::alloc::Allocator>>;

    auto allocator() const noexcept -> Self { return this->self(); }

public:
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        return allocator()->allocate(layout);
    }

    auto allocate_zeroed(Layout layout) const -> Result<Allocation, AllocError> {
        return allocator()->allocate_zeroed(layout);
    }

    void deallocate(void* ptr, Layout layout) const noexcept {
        allocator()->deallocate(ptr, layout);
    }

    auto grow(void* ptr, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        return allocator()->grow(ptr, old_layout, new_layout);
    }

    auto grow_zeroed(void* ptr, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        return allocator()->grow_zeroed(ptr, old_layout, new_layout);
    }

    auto shrink(void* ptr, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        return allocator()->shrink(ptr, old_layout, new_layout);
    }
};

// Explicit opt-in: copying Global or a borrowed allocator preserves its domain.
template<>
struct rstd::Impl<rstd::alloc::AllocatorClone, ::alloc::Global> {};
template<>
struct rstd::Impl<rstd::alloc::ThreadSafeAllocator, ::alloc::Global> {};
template<>
struct rstd::Impl<rstd::alloc::AllocatorClone, rstd::ref<rstd::dyn<rstd::alloc::Allocator>>> {};

export namespace rstd::alloc
{
template<typename A>
concept CloneableAllocator = Impled<A, Allocator, AllocatorClone> &&
                             (Impled<A, clone::Clone> || requires(const A& a) { A(a); });

template<typename A>
    requires CloneableAllocator<A>
auto clone_allocator(const A& allocator) -> A {
    if constexpr (Impled<A, clone::Clone>)
        return as<clone::Clone>(allocator).clone();
    else
        return A(allocator);
}
} // namespace rstd::alloc

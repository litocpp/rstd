module;
#if defined(__cpp_exceptions)
#include <new>
#endif

module rstd;
import :async.frame_allocator;
import rstd.alloc;

using namespace rstd;
using ::alloc::sync::SharedAllocator;
using rstd::alloc::Layout;

namespace
{
thread_local const SharedAllocator* current_allocator {};
thread_local bool                   scope_active {};

constexpr auto FRAME_TAG_SIZE      = usize(2);
constexpr auto FRAME_CUSTOM        = 1u;
constexpr auto FRAME_ALIGNED       = 2u;
constexpr auto SIZE_T_BIT_WIDTH    = sizeof(rstd::size_t) * __CHAR_BIT__;
constexpr auto MIN_FRAME_ALIGNMENT = alignof(void*) > alignof(Layout) ? alignof(void*)
                                                                      : alignof(Layout);
static_assert(SIZE_T_BIT_WIDTH <= 256);

[[noreturn]]
void invalid_frame_layout() {
#if defined(__cpp_exceptions)
    throw std::bad_alloc {};
#else
    rstd::panic { "invalid coroutine frame layout" };
#endif
}

[[noreturn]]
void frame_allocation_failed(Layout layout) {
#if defined(__cpp_exceptions)
    (void)layout;
    throw std::bad_alloc {};
#else
    ::alloc::handle_alloc_error(layout);
#endif
}

auto frame_allocation_layout(rstd::size_t size, usize alignment, bool custom, usize& owner_offset)
    -> Option<Layout> {
    if (! alignment.is_power_of_two()) return None();
    if (alignment < usize(MIN_FRAME_ALIGNMENT)) alignment = usize(MIN_FRAME_ALIGNMENT);
    auto frame_layout = Layout::from_size_align(usize(size), alignment);
    if (frame_layout.is_none()) return None();
    usize tag_offset {};
    auto  tag_layout = Layout::from_size_align_unchecked(FRAME_TAG_SIZE, usize(1));
    auto  layout     = frame_layout->extend(tag_layout, tag_offset);
    if (layout.is_none()) return None();
    if (custom) {
        auto owner_layout = Layout::from_size_align_unchecked(usize(sizeof(SharedAllocator)),
                                                              usize(alignof(SharedAllocator)));
        layout            = layout->extend(owner_layout, owner_offset);
    }
    return layout;
}

auto allocate_frame(rstd::size_t size, rstd::size_t alignment, bool aligned_global) -> void* {
    bool  custom = current_allocator != nullptr;
    usize owner_offset {};
    auto  allocation_layout = frame_allocation_layout(size, usize(alignment), custom, owner_offset);
    if (allocation_layout.is_none()) invalid_frame_layout();
    auto                    layout = allocation_layout.unwrap_unchecked();
    Option<SharedAllocator> owner {};
    if (custom) owner = Some(current_allocator->clone());
    void* base {};
    if (custom) {
        auto allocation = owner->allocate(layout);
        if (allocation.is_err()) frame_allocation_failed(layout);
        base = allocation.unwrap_unchecked().pointer;
    } else if (aligned_global) {
        base = ::operator new(layout.size.to_primitive(),
                              static_cast<rstd::align_val_t>(layout.align.to_primitive()));
    } else {
        base = ::operator new(layout.size.to_primitive());
    }
    // Allocator storage is a byte array whose tail outlives the coroutine frame.
    auto* bytes = static_cast<byte*>(base);
    bytes[size] =
        static_cast<byte>((custom ? FRAME_CUSTOM : 0u) | (aligned_global ? FRAME_ALIGNED : 0u));
    auto          remaining_alignment = alignment;
    unsigned char exponent {};
    while (remaining_alignment > 1) {
        remaining_alignment >>= 1;
        ++exponent;
    }
    bytes[size + 1] = static_cast<byte>(exponent);
    if (custom) {
        auto* allocator = reinterpret_cast<SharedAllocator*>(bytes + owner_offset.to_primitive());
        rstd::construct_at(allocator, rstd::move(*owner));
    }
    return base;
}
} // namespace
namespace rstd::async
{
FrameAllocatorScope::FrameAllocatorScope(const SharedAllocator* allocator) noexcept
    : m_previous(current_allocator), m_previous_active(scope_active) {
    current_allocator = allocator;
    scope_active      = true;
}
FrameAllocatorScope::~FrameAllocatorScope() {
    current_allocator = m_previous;
    scope_active      = m_previous_active;
}
auto has_frame_allocator_scope() noexcept -> bool {
    return scope_active;
}
auto current_frame_allocator() noexcept -> const SharedAllocator* {
    return current_allocator;
}
auto clone_current_frame_allocator() -> Option<SharedAllocator> {
    return current_allocator == nullptr ? None<SharedAllocator>()
                                        : Some(current_allocator->clone());
}
auto allocate_coro_frame(rstd::size_t size) -> void* {
    return allocate_frame(size, __STDCPP_DEFAULT_NEW_ALIGNMENT__, false);
}
auto allocate_coro_frame(rstd::size_t size, rstd::align_val_t alignment) -> void* {
    return allocate_frame(size, static_cast<rstd::size_t>(alignment), true);
}
void deallocate_coro_frame(void* frame, rstd::size_t size) noexcept {
    if (frame == nullptr) return;
    auto* bytes = static_cast<byte*>(frame);
    auto  flags = static_cast<unsigned char>(bytes[size]);
    if (flags == 0) {
#if defined(__cpp_sized_deallocation)
        auto allocation_size = usize(size).checked_add(FRAME_TAG_SIZE);
        if (allocation_size.is_none()) rstd::panic { "invalid coroutine frame layout" };
        ::operator delete(frame, allocation_size->to_primitive());
#else
        ::operator delete(frame);
#endif
        return;
    }
    auto exponent = static_cast<unsigned char>(bytes[size + 1]);
    if (exponent >= SIZE_T_BIT_WIDTH) rstd::panic { "invalid coroutine frame alignment" };
    auto  alignment = usize(static_cast<rstd::size_t>(1) << exponent);
    bool  custom    = (flags & FRAME_CUSTOM) != 0;
    usize owner_offset {};
    auto  reconstructed = frame_allocation_layout(size, alignment, custom, owner_offset);
    if (reconstructed.is_none()) rstd::panic { "invalid coroutine frame layout" };
    auto layout = reconstructed.unwrap_unchecked();
    if (custom) {
        auto* allocator =
            rstd::launder(reinterpret_cast<SharedAllocator*>(bytes + owner_offset.to_primitive()));
        auto owner = rstd::move(*allocator);
        rstd::destroy_at(allocator);
        owner.deallocate(frame, layout);
    } else {
        if ((flags & FRAME_ALIGNED) != 0) {
#if defined(__cpp_sized_deallocation)
            ::operator delete(frame,
                              layout.size.to_primitive(),
                              static_cast<rstd::align_val_t>(layout.align.to_primitive()));
#else
            ::operator delete(frame, static_cast<rstd::align_val_t>(layout.align.to_primitive()));
#endif
        } else {
#if defined(__cpp_sized_deallocation)
            ::operator delete(frame, layout.size.to_primitive());
#else
            ::operator delete(frame);
#endif
        }
    }
}
} // namespace rstd::async

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

struct FrameMarker {
    void*  base;
    Layout layout;
    bool   custom;
    bool   aligned_global;
};
// Keep the marker adjacent to the frame and the optional owner in a separate aligned slot.
constexpr rstd::size_t OWNER_ALIGNMENT = alignof(SharedAllocator);
constexpr rstd::size_t MARKER_PADDED =
    (sizeof(FrameMarker) + OWNER_ALIGNMENT - 1) & ~(OWNER_ALIGNMENT - 1);
constexpr rstd::size_t OWNER_OFFSET = MARKER_PADDED + sizeof(SharedAllocator);
static_assert(alignof(FrameMarker) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__);

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

auto frame_allocation_layout(rstd::size_t size, usize alignment, bool custom, usize& offset)
    -> Option<Layout> {
    if (! alignment.is_power_of_two()) return None();
    auto metadata_alignment = usize(alignof(FrameMarker));
    if (custom && metadata_alignment < usize(OWNER_ALIGNMENT))
        metadata_alignment = usize(OWNER_ALIGNMENT);
    if (alignment < metadata_alignment) alignment = metadata_alignment;
    auto metadata_size   = usize(custom ? OWNER_OFFSET : sizeof(FrameMarker));
    auto metadata_layout = Layout::from_size_align(metadata_size, metadata_alignment);
    auto frame_layout    = Layout::from_size_align(usize(size), alignment);
    if (metadata_layout.is_none() || frame_layout.is_none()) return None();
    return metadata_layout->extend(*frame_layout, offset);
}

auto allocate_frame(rstd::size_t size, rstd::size_t alignment, bool aligned_global) -> void* {
    bool  custom = current_allocator != nullptr;
    usize frame_offset {};
    auto  allocation_layout = frame_allocation_layout(size, usize(alignment), custom, frame_offset);
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
    auto* frame = static_cast<byte*>(base) + frame_offset.to_primitive();
    auto  info  = FrameMarker { base, layout, custom, aligned_global };
    if (custom) {
        auto* allocator = reinterpret_cast<SharedAllocator*>(frame - OWNER_OFFSET);
        rstd::construct_at(allocator, rstd::move(*owner));
    }
    auto* marker = reinterpret_cast<FrameMarker*>(frame - sizeof(FrameMarker));
    rstd::construct_at(marker, info);
    return frame;
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
void deallocate_coro_frame(void* frame) noexcept {
    if (frame == nullptr) return;
    auto* bytes  = static_cast<byte*>(frame);
    auto* marker = rstd::launder(reinterpret_cast<FrameMarker*>(bytes - sizeof(FrameMarker)));
    auto  info   = *marker;
    auto  layout = info.layout;
    auto* base   = info.base;
    if (info.custom) {
        auto* allocator = rstd::launder(reinterpret_cast<SharedAllocator*>(bytes - OWNER_OFFSET));
        auto  owner     = rstd::move(*allocator);
        rstd::destroy_at(allocator);
        rstd::destroy_at(marker);
        owner.deallocate(base, layout);
    } else {
        rstd::destroy_at(marker);
        if (info.aligned_global) {
#if defined(__cpp_sized_deallocation)
            ::operator delete(base,
                              layout.size.to_primitive(),
                              static_cast<rstd::align_val_t>(layout.align.to_primitive()));
#else
            ::operator delete(base, static_cast<rstd::align_val_t>(layout.align.to_primitive()));
#endif
        } else {
#if defined(__cpp_sized_deallocation)
            ::operator delete(base, layout.size.to_primitive());
#else
            ::operator delete(base);
#endif
        }
    }
}
} // namespace rstd::async

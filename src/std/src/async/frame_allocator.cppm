export module rstd:async.frame_allocator;
import rstd.alloc;

export namespace rstd::async
{

using FrameAllocatorRef = ref<dyn<rstd::alloc::Allocator>>;

/// The allocator must support cross-thread use and outlive frames created in this scope.
class FrameAllocatorScope {
    Option<FrameAllocatorRef> m_previous;
    bool                      m_previous_active;

public:
    explicit FrameAllocatorScope(Option<FrameAllocatorRef> allocator) noexcept;
    FrameAllocatorScope(const FrameAllocatorScope&)                    = delete;
    auto operator=(const FrameAllocatorScope&) -> FrameAllocatorScope& = delete;
    ~FrameAllocatorScope();
};

auto has_frame_allocator_scope() noexcept -> bool;
auto current_frame_allocator() noexcept -> Option<FrameAllocatorRef>;

auto allocate_coro_frame(rstd::size_t size) -> void*;
auto allocate_coro_frame(rstd::size_t size, rstd::align_val_t alignment) -> void*;
void deallocate_coro_frame(void* frame, rstd::size_t size) noexcept;

class CoroFrameAllocation {
public:
    static auto operator new(rstd::size_t size) -> void* { return allocate_coro_frame(size); }

    static auto operator new(rstd::size_t size, rstd::align_val_t alignment) -> void* {
        return allocate_coro_frame(size, alignment);
    }

    static void operator delete(void* frame, rstd::size_t size) noexcept {
        deallocate_coro_frame(frame, size);
    }

    static void operator delete(void* frame, rstd::size_t size, rstd::align_val_t) noexcept {
        deallocate_coro_frame(frame, size);
    }
};

} // namespace rstd::async

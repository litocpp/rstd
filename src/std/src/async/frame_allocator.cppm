export module rstd:async.frame_allocator;
import rstd.alloc;

export namespace rstd::async
{

class FrameAllocatorScope {
    const ::alloc::sync::SharedAllocator* m_previous;
    bool                                  m_previous_active;

public:
    explicit FrameAllocatorScope(const ::alloc::sync::SharedAllocator* allocator) noexcept;
    FrameAllocatorScope(const FrameAllocatorScope&)                    = delete;
    auto operator=(const FrameAllocatorScope&) -> FrameAllocatorScope& = delete;
    ~FrameAllocatorScope();
};

auto has_frame_allocator_scope() noexcept -> bool;
auto current_frame_allocator() noexcept -> const ::alloc::sync::SharedAllocator*;
auto clone_current_frame_allocator() -> Option<::alloc::sync::SharedAllocator>;

auto allocate_coro_frame(rstd::size_t size) -> void*;
auto allocate_coro_frame(rstd::size_t size, rstd::align_val_t alignment) -> void*;
void deallocate_coro_frame(void* frame) noexcept;

} // namespace rstd::async

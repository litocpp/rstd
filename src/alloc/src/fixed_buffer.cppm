module;
#include <rstd/macro.hpp>

export module rstd.alloc:fixed_buffer;
import :alloc;

using namespace rstd::prelude;

export namespace rstd::alloc
{
struct FixedBufferStatistics {
    usize capacity_bytes {};
    usize used_bytes {};
    usize padding_bytes {};
};

/// Borrows CPU storage without fallback. Individual frees do not reclaim capacity.
/// Used bytes include padding and remain consumed until reset or a last-block shrink.
class FixedBufferAllocator {
    byte*         base_;
    usize         capacity_;
    mutable usize cursor_ {};
    mutable usize padding_ {};

    auto offset_of(void* pointer, Layout layout) const noexcept -> Option<usize> {
        if (layout.size == usize()) return None();
        auto address = usize(reinterpret_cast<uintptr_t>(pointer));
        auto base    = usize(reinterpret_cast<uintptr_t>(base_));
        if (address < base) return None();
        auto offset = address - base;
        if (offset > cursor_ || layout.size > cursor_ - offset) return None();
        return Some(offset);
    }

    static auto aligned(void* pointer, usize alignment) noexcept -> bool {
        return (usize(reinterpret_cast<uintptr_t>(pointer)) & (alignment - usize(1))) == usize();
    }

public:
    explicit FixedBufferAllocator(mut_ptr<u8[]> buffer [[clang::lifetimebound]]) noexcept
        : base_(buffer.as_raw_ptr()), capacity_(buffer.len()) {
        debug_assert(base_ != nullptr || capacity_ == usize());
    }

    FixedBufferAllocator(const FixedBufferAllocator&)                    = delete;
    FixedBufferAllocator(FixedBufferAllocator&&)                         = delete;
    auto operator=(const FixedBufferAllocator&) -> FixedBufferAllocator& = delete;
    auto operator=(FixedBufferAllocator&&) -> FixedBufferAllocator&      = delete;

    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        if (layout.size == usize()) return Ok(Allocation { layout.dangling(), usize() });
        auto base    = usize(reinterpret_cast<uintptr_t>(base_));
        auto address = base.checked_add(cursor_);
        if (address.is_none()) return Err(AllocError {});
        auto mask   = layout.align - usize(1);
        auto padded = address->checked_add(mask);
        if (padded.is_none()) return Err(AllocError {});
        auto offset = (*padded & ~mask) - base;
        auto end    = offset.checked_add(layout.size);
        if (end.is_none() || *end > capacity_) return Err(AllocError {});
        padding_ += offset - cursor_;
        cursor_ = *end;
        return Ok(Allocation { base_ + offset.to_primitive(), layout.size });
    }

    auto grow(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size >= old_layout.size);
        auto offset = offset_of(pointer, old_layout);
        if (offset.is_some() && *offset + old_layout.size == cursor_ &&
            aligned(pointer, new_layout.align)) {
            auto end = offset->checked_add(new_layout.size);
            if (end.is_none() || *end > capacity_) return Err(AllocError {});
            cursor_ = *end;
            return Ok(Allocation { pointer, new_layout.size });
        }
        auto allocation = allocate(new_layout);
        if (allocation.is_err()) return allocation;
        if (old_layout.size != usize()) mem::memcpy(allocation->pointer, pointer, old_layout.size);
        return allocation;
    }

    auto grow_zeroed(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        auto allocation = grow(pointer, old_layout, new_layout);
        if (allocation.is_ok() && allocation->size > old_layout.size)
            mem::memset(static_cast<byte*>(allocation->pointer) + old_layout.size.to_primitive(),
                        u8(),
                        allocation->size - old_layout.size);
        return allocation;
    }

    auto shrink(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size <= old_layout.size);
        auto offset = offset_of(pointer, old_layout);
        auto last   = offset.is_some() && *offset + old_layout.size == cursor_;
        if (new_layout.size == usize()) {
            if (last) cursor_ = *offset;
            return Ok(Allocation { new_layout.dangling(), usize() });
        }
        if (aligned(pointer, new_layout.align)) {
            if (last) cursor_ = *offset + new_layout.size;
            return Ok(Allocation { pointer, last ? new_layout.size : old_layout.size });
        }
        auto allocation = allocate(new_layout);
        if (allocation.is_err()) return allocation;
        mem::memcpy(allocation->pointer, pointer, new_layout.size);
        return allocation;
    }

    void deallocate(void*, Layout) const noexcept {}

    /// All objects and borrows from previous allocations must have ended before reset.
    void reset() noexcept { cursor_ = padding_ = usize(); }

    auto statistics() const noexcept -> FixedBufferStatistics {
        return { capacity_, cursor_, padding_ };
    }
};
} // namespace rstd::alloc

namespace rstd
{
template<>
struct Impl<alloc::Allocator, alloc::FixedBufferAllocator>
    : DefaultInImpl<alloc::Allocator, alloc::FixedBufferAllocator> {
    auto allocate(alloc::Layout layout) const { return this->self().allocate(layout); }
    auto grow(void* pointer, alloc::Layout old_layout, alloc::Layout new_layout) const {
        return this->self().grow(pointer, old_layout, new_layout);
    }
    auto grow_zeroed(void* pointer, alloc::Layout old_layout, alloc::Layout new_layout) const {
        return this->self().grow_zeroed(pointer, old_layout, new_layout);
    }
    auto shrink(void* pointer, alloc::Layout old_layout, alloc::Layout new_layout) const {
        return this->self().shrink(pointer, old_layout, new_layout);
    }
    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        this->self().deallocate(pointer, layout);
    }
};
} // namespace rstd

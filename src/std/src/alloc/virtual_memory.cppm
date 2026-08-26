module;
#include <rstd/macro.hpp>

export module rstd:alloc.virtual_memory;
import rstd.alloc;

#if RSTD_OS_UNIX
import :alloc.unix;
#elif RSTD_OS_WINDOWS
import :alloc.windows;
#else
#error "rstd::alloc::VirtualMemoryAllocator is unsupported on this platform"
#endif

using namespace rstd::prelude;
using rstd::alloc::Allocation;
using rstd::alloc::Allocator;
using rstd::alloc::AllocError;
using rstd::alloc::Layout;
using rstd::result::Result;

namespace rstd::alloc
{

#if RSTD_OS_UNIX
namespace virtual_memory_backend = rstd::sys::alloc::unix;
#elif RSTD_OS_WINDOWS
namespace virtual_memory_backend = rstd::sys::alloc::windows;
#endif

export struct VirtualMemoryStatistics {
    usize mapped_bytes;
    usize mapped_bytes_peak;
    usize mappings;
    usize mappings_peak;
    usize allocation_calls;
    usize deallocation_calls;
};

export class VirtualMemoryAllocator {
    usize                           page_size_;
    usize                           mapping_alignment_;
    mutable VirtualMemoryStatistics statistics_;

    auto rounded_size(Layout layout) const noexcept -> Option<usize> {
        if (layout.align > mapping_alignment_) return None();
        auto const remainder = layout.size % page_size_;
        if (remainder == usize()) return Some(layout.size);
        return layout.size.checked_add(page_size_ - remainder);
    }

public:
    VirtualMemoryAllocator() noexcept
        : VirtualMemoryAllocator(virtual_memory_backend::information()) {}

private:
    explicit VirtualMemoryAllocator(virtual_memory_backend::Information information) noexcept
        : page_size_(information.page_size),
          mapping_alignment_(information.mapping_alignment),
          statistics_() {}

public:
    VirtualMemoryAllocator(const VirtualMemoryAllocator&)                    = delete;
    auto operator=(const VirtualMemoryAllocator&) -> VirtualMemoryAllocator& = delete;

    VirtualMemoryAllocator(VirtualMemoryAllocator&& other) noexcept
        : page_size_(other.page_size_),
          mapping_alignment_(other.mapping_alignment_),
          statistics_(other.statistics_) {
        other.statistics_ = {};
    }

    auto operator=(VirtualMemoryAllocator&&) -> VirtualMemoryAllocator& = delete;

    auto allocate(Layout layout) const noexcept -> Result<Allocation, AllocError> {
        ++statistics_.allocation_calls;
        if (layout.size == usize()) return Ok(Allocation { layout.dangling(), usize() });

        auto mapping_size = rounded_size(layout);
        if (mapping_size.is_none()) return Err(AllocError {});
        auto* pointer = virtual_memory_backend::map(*mapping_size);
        if (pointer == nullptr) return Err(AllocError {});

        statistics_.mapped_bytes += *mapping_size;
        ++statistics_.mappings;
        if (statistics_.mapped_bytes > statistics_.mapped_bytes_peak) {
            statistics_.mapped_bytes_peak = statistics_.mapped_bytes;
        }
        if (statistics_.mappings > statistics_.mappings_peak) {
            statistics_.mappings_peak = statistics_.mappings;
        }
        return Ok(Allocation { pointer, layout.size });
    }

    auto allocate_zeroed(Layout layout) const noexcept -> Result<Allocation, AllocError> {
        return allocate(layout);
    }

    void deallocate(void* pointer, Layout layout) const noexcept {
        if (layout.size == usize()) return;
        auto mapping_size = rounded_size(layout);
        debug_assert(mapping_size.is_some());
        if (mapping_size.is_none()) return;
        auto const released = virtual_memory_backend::unmap(pointer, *mapping_size);
        debug_assert(released);
        if (! released) return;
        statistics_.mapped_bytes -= *mapping_size;
        --statistics_.mappings;
        ++statistics_.deallocation_calls;
    }

    constexpr auto page_size() const noexcept -> usize { return page_size_; }

    constexpr auto mapping_alignment() const noexcept -> usize { return mapping_alignment_; }

    constexpr auto statistics() const noexcept -> VirtualMemoryStatistics { return statistics_; }
};

export using VirtualMemoryArena = ::alloc::BumpArena<VirtualMemoryAllocator>;

} // namespace rstd::alloc

namespace rstd
{

template<>
struct Impl<alloc::Allocator, alloc::VirtualMemoryAllocator>
    : DefaultInImpl<alloc::Allocator, alloc::VirtualMemoryAllocator> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        return this->self().allocate(layout);
    }

    auto allocate_zeroed(alloc::Layout layout) const
        -> Result<alloc::Allocation, alloc::AllocError> {
        return this->self().allocate_zeroed(layout);
    }

    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        this->self().deallocate(pointer, layout);
    }
};

} // namespace rstd

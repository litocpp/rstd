module;
#include <rstd/macro.hpp>

export module rstd:alloc.virtual_memory;
import rstd.alloc;
import rstd.sys.mem;
import :sync.mutex;

#if ! RSTD_OS_UNIX && ! RSTD_OS_WINDOWS
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

export struct VirtualMemoryStatistics {
    usize mapped_bytes;
    usize mapped_bytes_peak;
    usize mappings;
    usize mappings_peak;
    usize allocation_calls;
    usize deallocation_calls;
    usize allocated_bytes;
    usize released_bytes;
};

export class VirtualMemoryAllocator {
    usize                                              page_size_;
    usize                                              mapping_alignment_;
    mutable rstd::sync::Mutex<VirtualMemoryStatistics> statistics_;

    auto rounded_size(Layout layout) const noexcept -> Option<usize> {
        if (layout.align > mapping_alignment_) return None();
        auto const remainder = layout.size % page_size_;
        if (remainder == usize()) return Some(layout.size);
        return layout.size.checked_add(page_size_ - remainder);
    }

public:
    VirtualMemoryAllocator() noexcept: VirtualMemoryAllocator(sys::mem::information()) {}

private:
    explicit VirtualMemoryAllocator(sys::mem::Information information) noexcept
        : page_size_(information.page_size),
          mapping_alignment_(information.mapping_alignment),
          statistics_() {}

public:
    VirtualMemoryAllocator(const VirtualMemoryAllocator&)                    = delete;
    auto operator=(const VirtualMemoryAllocator&) -> VirtualMemoryAllocator& = delete;

    VirtualMemoryAllocator(VirtualMemoryAllocator&& other) noexcept
        : page_size_(other.page_size_),
          mapping_alignment_(other.mapping_alignment_),
          statistics_(other.statistics()) {
        *other.statistics_.lock().unwrap_unchecked() = {};
    }

    auto operator=(VirtualMemoryAllocator&&) -> VirtualMemoryAllocator& = delete;

    auto allocate(Layout layout) const noexcept -> Result<Allocation, AllocError> {
        auto  guard      = statistics_.lock().unwrap_unchecked();
        auto& statistics = *guard;
        ++statistics.allocation_calls;
        if (layout.size == usize()) return Ok(Allocation { layout.dangling(), usize() });

        auto mapping_size = rounded_size(layout);
        if (mapping_size.is_none()) return Err(AllocError {});
        auto* pointer = sys::mem::map(*mapping_size);
        if (pointer == nullptr) return Err(AllocError {});

        statistics.mapped_bytes += *mapping_size;
        statistics.allocated_bytes += *mapping_size;
        ++statistics.mappings;
        if (statistics.mapped_bytes > statistics.mapped_bytes_peak) {
            statistics.mapped_bytes_peak = statistics.mapped_bytes;
        }
        if (statistics.mappings > statistics.mappings_peak) {
            statistics.mappings_peak = statistics.mappings;
        }
        auto* storage = ::new (pointer) byte[mapping_size->to_primitive()];
        return Ok(Allocation { storage, layout.size });
    }

    auto allocate_zeroed(Layout layout) const noexcept -> Result<Allocation, AllocError> {
        return allocate(layout);
    }

    void deallocate(void* pointer, Layout layout) const noexcept {
        if (layout.size == usize()) return;
        auto  guard        = statistics_.lock().unwrap_unchecked();
        auto& statistics   = *guard;
        auto  mapping_size = rounded_size(layout);
        debug_assert(mapping_size.is_some());
        if (mapping_size.is_none()) return;
        auto const released = sys::mem::unmap(pointer, *mapping_size);
        debug_assert(released);
        if (! released) return;
        statistics.mapped_bytes -= *mapping_size;
        statistics.released_bytes += *mapping_size;
        --statistics.mappings;
        ++statistics.deallocation_calls;
    }

    auto allocation_size(Layout layout) const noexcept -> Option<usize> {
        return rounded_size(layout);
    }
    constexpr auto page_size() const noexcept -> usize { return page_size_; }

    constexpr auto mapping_alignment() const noexcept -> usize { return mapping_alignment_; }

    auto statistics() const noexcept -> VirtualMemoryStatistics {
        return *statistics_.lock().unwrap_unchecked();
    }
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

template<>
struct rstd::Impl<rstd::alloc::TransferableAllocator, rstd::alloc::VirtualMemoryAllocator> {};

template<>
struct rstd::Impl<rstd::alloc::ThreadSafeAllocator, rstd::alloc::VirtualMemoryAllocator> {};

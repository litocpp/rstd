module;
#include <rstd/macro.hpp>

export module rstd.alloc:arena;
export import :alloc;
export import :vec;

using namespace rstd::prelude;

using rstd::alloc::Allocation;
using rstd::alloc::Allocator;
using rstd::alloc::AllocError;
using rstd::alloc::Layout;
using rstd::result::Result;

namespace alloc
{

export struct ArenaStats {
    usize used_bytes;
    usize reserved_bytes;
    usize padding_bytes;
    usize ordinary_slabs;
    usize large_slabs;
    usize peak_used_bytes;
};

export struct RecyclingArenaStats {
    usize live_bytes;
    usize peak_live_bytes;
    usize reserved_bytes;
    usize free_bytes;
    usize reused_bytes;
    usize ordinary_slabs;
    usize large_slabs;
    usize allocations;
    usize reuses;
};

struct ArenaSlab {
    void*  pointer;
    Layout layout;
    usize  cursor;
    bool   large;
};

struct RecycledAllocation {
    void*  pointer;
    Layout layout;
};

export template<typename Upstream>
class BumpArena;

export template<typename Upstream>
class RecyclingArena;

export template<typename Upstream = Global>
class ArenaAllocator {
    BumpArena<Upstream>* arena_;

    constexpr explicit ArenaAllocator(BumpArena<Upstream>& arena) noexcept: arena_(&arena) {}

    friend class BumpArena<Upstream>;

public:
    constexpr ArenaAllocator(const ArenaAllocator&) noexcept                    = default;
    constexpr auto operator=(const ArenaAllocator&) noexcept -> ArenaAllocator& = default;

    constexpr auto arena() const noexcept [[clang::lifetimebound]] -> BumpArena<Upstream>& {
        return *arena_;
    }
};

export template<typename Upstream = Global>
class RecyclingArenaAllocator {
    RecyclingArena<Upstream>* arena_;

    constexpr explicit RecyclingArenaAllocator(RecyclingArena<Upstream>& arena) noexcept
        : arena_(&arena) {}

    friend class RecyclingArena<Upstream>;

public:
    constexpr RecyclingArenaAllocator(const RecyclingArenaAllocator&) noexcept = default;
    constexpr auto operator=(const RecyclingArenaAllocator&) noexcept
        -> RecyclingArenaAllocator& = default;

    constexpr auto arena() const noexcept [[clang::lifetimebound]] -> RecyclingArena<Upstream>& {
        return *arena_;
    }
};

export template<typename Upstream = Global>
class BumpArena {
    static constexpr usize DEFAULT_SLAB_SIZE = usize(64 * 1024);
    static constexpr usize NO_SLAB           = usize::MAX;

    RSTD_ATTR_NO_UNIQUE_ADDRESS Upstream upstream_;
    alloc::vec::Vec<ArenaSlab>           slabs_;
    usize                                slab_size_;
    usize                                current_slab_;
    ArenaStats                           stats_;

    auto allocate_upstream(Layout layout) -> Result<Allocation, AllocError> {
        return as<Allocator>(upstream_).allocate(layout);
    }

    auto try_allocate(ArenaSlab& slab, Layout layout) noexcept -> void* {
        auto const base    = reinterpret_cast<uintptr_t>(slab.pointer);
        auto const current = base + slab.cursor.to_primitive();
        auto const mask    = layout.align.to_primitive() - 1;
        if (current > uintptr_t(-1) - mask) return nullptr;
        auto const aligned  = (current + mask) & ~mask;
        auto const padding  = usize(aligned - current);
        auto const occupied = padding.checked_add(layout.size);
        if (occupied.is_none()) return nullptr;
        auto const remaining = slab.layout.size - slab.cursor;
        if (*occupied > remaining) return nullptr;

        slab.cursor += *occupied;
        stats_.used_bytes += layout.size;
        stats_.padding_bytes += padding;
        if (stats_.used_bytes > stats_.peak_used_bytes) {
            stats_.peak_used_bytes = stats_.used_bytes;
        }
        return reinterpret_cast<void*>(aligned);
    }

    auto allocate_large(Layout layout) -> Result<Allocation, AllocError> {
        auto allocation = allocate_upstream(layout);
        if (allocation.is_err()) return allocation;
        auto value = allocation.unwrap_unchecked();
        slabs_.emplace_back(ArenaSlab {
            .pointer = value.pointer, .layout = layout, .cursor = layout.size, .large = true });
        stats_.used_bytes += layout.size;
        stats_.reserved_bytes += layout.size;
        ++stats_.large_slabs;
        if (stats_.used_bytes > stats_.peak_used_bytes) {
            stats_.peak_used_bytes = stats_.used_bytes;
        }
        return Ok(value);
    }

    auto add_ordinary_slab(Layout request) -> Result<Allocation, AllocError> {
        auto const alignment  = request.align > usize(16) ? request.align : usize(16);
        auto const layout     = Layout::from_size_align(slab_size_, alignment).unwrap_unchecked();
        auto       allocation = allocate_upstream(layout);
        if (allocation.is_err()) return allocation;

        auto value = allocation.unwrap_unchecked();
        slabs_.emplace_back(ArenaSlab {
            .pointer = value.pointer, .layout = layout, .cursor = usize(), .large = false });
        current_slab_ = slabs_.len() - usize(1);
        stats_.reserved_bytes += layout.size;
        ++stats_.ordinary_slabs;
        auto pointer = try_allocate(slabs_[current_slab_], request);
        debug_assert(pointer != nullptr);
        return Ok(Allocation { pointer, request.size });
    }

public:
    explicit BumpArena(usize slab_size = DEFAULT_SLAB_SIZE, Upstream upstream = Upstream {})
        : upstream_(rstd::move(upstream)),
          slabs_(),
          slab_size_(slab_size < usize(1024) ? usize(1024) : slab_size),
          current_slab_(NO_SLAB),
          stats_() {}

    BumpArena(const BumpArena&)                    = delete;
    auto operator=(const BumpArena&) -> BumpArena& = delete;
    BumpArena(BumpArena&&)                         = delete;
    auto operator=(BumpArena&&) -> BumpArena&      = delete;

    ~BumpArena() {
        for (auto& slab : slabs_) {
            as<Allocator>(upstream_).deallocate(slab.pointer, slab.layout);
        }
    }

    auto allocate(Layout layout) -> Result<Allocation, AllocError> {
        if (layout.size == usize()) {
            return Ok(Allocation { layout.dangling(), usize() });
        }

        if (layout.size > slab_size_ / usize(2) || layout.align > slab_size_ / usize(2)) {
            return allocate_large(layout);
        }

        if (current_slab_ != NO_SLAB) {
            auto& slab = slabs_[current_slab_];
            if (layout.align <= slab.layout.align) {
                if (auto* pointer = try_allocate(slab, layout); pointer != nullptr) {
                    return Ok(Allocation { pointer, layout.size });
                }
            }
        }
        return add_ordinary_slab(layout);
    }

    constexpr auto allocator() noexcept [[clang::lifetimebound]] -> ArenaAllocator<Upstream> {
        return ArenaAllocator<Upstream>(*this);
    }

    auto upstream_statistics() const
        requires requires(const Upstream& upstream) { upstream.statistics(); }
    {
        return upstream_.statistics();
    }

    constexpr auto stats() const noexcept -> ArenaStats { return stats_; }
};

export template<typename Upstream = Global>
class RecyclingArena {
    static constexpr usize NO_SLAB = usize::MAX;

    RSTD_ATTR_NO_UNIQUE_ADDRESS Upstream upstream_;
    alloc::vec::Vec<ArenaSlab>           slabs_;
    alloc::vec::Vec<RecycledAllocation>  free_;
    usize                                slab_size_;
    usize                                current_slab_;
    usize                                physical_allocations_;
    RecyclingArenaStats                  stats_;

    auto allocate_upstream(Layout layout) -> Result<Allocation, AllocError> {
        return as<Allocator>(upstream_).allocate(layout);
    }

    auto try_allocate(ArenaSlab& slab, Layout layout) noexcept -> void* {
        auto const base    = reinterpret_cast<uintptr_t>(slab.pointer);
        auto const current = base + slab.cursor.to_primitive();
        auto const mask    = layout.align.to_primitive() - 1;
        if (current > uintptr_t(-1) - mask) return nullptr;
        auto const aligned  = (current + mask) & ~mask;
        auto const padding  = usize(aligned - current);
        auto const occupied = padding.checked_add(layout.size);
        if (occupied.is_none() || *occupied > slab.layout.size - slab.cursor) return nullptr;
        slab.cursor += *occupied;
        return reinterpret_cast<void*>(aligned);
    }

    auto record_allocation(Layout layout) noexcept -> void {
        stats_.live_bytes += layout.size;
        ++stats_.allocations;
        if (stats_.live_bytes > stats_.peak_live_bytes) {
            stats_.peak_live_bytes = stats_.live_bytes;
        }
    }

    auto prepare_fresh_allocation() -> void {
        auto required = physical_allocations_ + usize(1);
        if (free_.capacity() < required) free_.reserve(required - free_.len());
        physical_allocations_ = required;
    }

    auto reuse(Layout layout) -> Option<Allocation> {
        for (auto index = usize {}; index < free_.len(); ++index) {
            if (free_[index].layout.size != layout.size ||
                free_[index].layout.align != layout.align) {
                continue;
            }
            auto value = rstd::move(free_[index]);
            auto last  = free_.pop().unwrap();
            if (index < free_.len()) free_[index] = rstd::move(last);
            stats_.free_bytes -= layout.size;
            stats_.reused_bytes += layout.size;
            ++stats_.reuses;
            record_allocation(layout);
            return Some(Allocation { value.pointer, layout.size });
        }
        return None();
    }

    auto allocate_large(Layout layout) -> Result<Allocation, AllocError> {
        auto allocation = allocate_upstream(layout);
        if (allocation.is_err()) return allocation;
        auto value = allocation.unwrap_unchecked();
        slabs_.emplace_back(ArenaSlab {
            .pointer = value.pointer, .layout = layout, .cursor = layout.size, .large = true });
        prepare_fresh_allocation();
        stats_.reserved_bytes += layout.size;
        ++stats_.large_slabs;
        record_allocation(layout);
        return Ok(value);
    }

    auto add_ordinary_slab(Layout request) -> Result<Allocation, AllocError> {
        auto const alignment  = request.align > usize(16) ? request.align : usize(16);
        auto const layout     = Layout::from_size_align(slab_size_, alignment).unwrap_unchecked();
        auto       allocation = allocate_upstream(layout);
        if (allocation.is_err()) return allocation;
        auto value = allocation.unwrap_unchecked();
        slabs_.emplace_back(ArenaSlab {
            .pointer = value.pointer, .layout = layout, .cursor = usize {}, .large = false });
        current_slab_ = slabs_.len() - usize(1);
        stats_.reserved_bytes += layout.size;
        ++stats_.ordinary_slabs;
        auto* pointer = try_allocate(slabs_[current_slab_], request);
        debug_assert(pointer != nullptr);
        prepare_fresh_allocation();
        record_allocation(request);
        return Ok(Allocation { pointer, request.size });
    }

public:
    explicit RecyclingArena(usize slab_size = usize(64 * 1024), Upstream upstream = Upstream {})
        : upstream_(rstd::move(upstream)),
          slabs_(),
          free_(),
          slab_size_(slab_size < usize(1024) ? usize(1024) : slab_size),
          current_slab_(NO_SLAB),
          physical_allocations_(),
          stats_() {}

    RecyclingArena(const RecyclingArena&)                    = delete;
    auto operator=(const RecyclingArena&) -> RecyclingArena& = delete;
    RecyclingArena(RecyclingArena&&)                         = delete;
    auto operator=(RecyclingArena&&) -> RecyclingArena&      = delete;

    ~RecyclingArena() {
        for (auto& slab : slabs_) {
            as<Allocator>(upstream_).deallocate(slab.pointer, slab.layout);
        }
    }

    auto allocate(Layout layout) -> Result<Allocation, AllocError> {
        if (layout.size == usize()) {
            return Ok(Allocation { layout.dangling(), usize() });
        }
        auto recycled = reuse(layout);
        if (recycled.is_some()) return Ok(rstd::move(recycled).unwrap());
        if (layout.size > slab_size_ / usize(2) || layout.align > slab_size_ / usize(2)) {
            return allocate_large(layout);
        }
        if (current_slab_ != NO_SLAB) {
            auto& slab = slabs_[current_slab_];
            if (layout.align <= slab.layout.align) {
                if (auto* pointer = try_allocate(slab, layout); pointer != nullptr) {
                    prepare_fresh_allocation();
                    record_allocation(layout);
                    return Ok(Allocation { pointer, layout.size });
                }
            }
        }
        return add_ordinary_slab(layout);
    }

    void deallocate(void* pointer, Layout layout) noexcept {
        if (layout.size == usize()) return;
        debug_assert(stats_.live_bytes >= layout.size);
        stats_.live_bytes -= layout.size;
        stats_.free_bytes += layout.size;
        debug_assert(free_.len() < free_.capacity());
        free_.push(RecycledAllocation { .pointer = pointer, .layout = layout });
    }

    constexpr auto allocator() noexcept [[clang::lifetimebound]]
    -> RecyclingArenaAllocator<Upstream> {
        return RecyclingArenaAllocator<Upstream>(*this);
    }

    auto upstream_statistics() const
        requires requires(const Upstream& upstream) { upstream.statistics(); }
    {
        return upstream_.statistics();
    }

    constexpr auto stats() const noexcept -> RecyclingArenaStats { return stats_; }
};

} // namespace alloc

namespace rstd
{

template<typename Upstream>
struct Impl<alloc::Allocator, ::alloc::ArenaAllocator<Upstream>>
    : DefaultInImpl<alloc::Allocator, ::alloc::ArenaAllocator<Upstream>> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        return this->self().arena().allocate(layout);
    }

    void deallocate(void*, alloc::Layout) const noexcept {}
};

template<typename Upstream>
struct Impl<alloc::Allocator, ::alloc::RecyclingArenaAllocator<Upstream>>
    : DefaultInImpl<alloc::Allocator, ::alloc::RecyclingArenaAllocator<Upstream>> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        return this->self().arena().allocate(layout);
    }

    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        this->self().arena().deallocate(pointer, layout);
    }
};

} // namespace rstd

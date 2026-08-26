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
    usize layout_classes;
    usize recycled_capacity;
    usize metadata_used_bytes;
    usize metadata_reserved_bytes;
    usize metadata_blocks;
};

struct ArenaSlab {
    void*  pointer;
    Layout layout;
    usize  cursor;
    bool   large;
};

template<typename A>
struct RecycledLayoutClass {
    Layout                    layout;
    alloc::vec::Vec<void*, A> pointers;
    usize                     physical_allocations;
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
    static constexpr usize NO_SLAB            = usize::MAX;
    static constexpr usize METADATA_SLAB_SIZE = usize(64 * 1024);

    using MetadataUpstream  = rstd::ref<rstd::dyn<Allocator>>;
    using MetadataArena     = BumpArena<MetadataUpstream>;
    using MetadataAllocator = ArenaAllocator<MetadataUpstream>;
    using LayoutClass       = RecycledLayoutClass<MetadataAllocator>;

    RSTD_ATTR_NO_UNIQUE_ADDRESS Upstream            upstream_;
    MetadataArena                                   metadata_;
    alloc::vec::Vec<ArenaSlab>                      slabs_;
    alloc::vec::Vec<LayoutClass, MetadataAllocator> recycled_;
    usize                                           slab_size_;
    usize                                           current_slab_;
    RecyclingArenaStats                             stats_;

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

    auto recycled_class(Layout layout) const noexcept -> Option<usize> {
        for (auto index = usize {}; index < recycled_.len(); ++index) {
            if (recycled_[index].layout.size == layout.size &&
                recycled_[index].layout.align == layout.align) {
                return Some(index);
            }
        }
        return None();
    }

    auto prepare_fresh_allocation(Layout layout) -> void {
        auto found = recycled_class(layout);
        if (found.is_none()) {
            recycled_.push(LayoutClass {
                .layout = layout,
                .pointers =
                    alloc::vec::Vec<void*, MetadataAllocator>::new_in(metadata_.allocator()),
                .physical_allocations = usize {},
            });
            found                 = Some(recycled_.len() - usize(1));
            stats_.layout_classes = recycled_.len();
        }
        auto& recycled = recycled_[*found];
        auto  required = recycled.physical_allocations + usize(1);
        if (recycled.pointers.capacity() < required) {
            auto previous = recycled.pointers.capacity();
            recycled.pointers.reserve(required - recycled.pointers.len());
            stats_.recycled_capacity += recycled.pointers.capacity() - previous;
        }
        recycled.physical_allocations = required;
    }

    auto reuse(Layout layout) -> Option<Allocation> {
        auto found = recycled_class(layout);
        if (found.is_none()) return None();
        auto& recycled = recycled_[*found];
        if (recycled.pointers.is_empty()) return None();
        auto pointer = recycled.pointers.pop().unwrap();
        stats_.free_bytes -= layout.size;
        stats_.reused_bytes += layout.size;
        ++stats_.reuses;
        record_allocation(layout);
        return Some(Allocation { pointer, layout.size });
    }

    auto allocate_large(Layout layout) -> Result<Allocation, AllocError> {
        auto allocation = allocate_upstream(layout);
        if (allocation.is_err()) return allocation;
        auto value = allocation.unwrap_unchecked();
        slabs_.emplace_back(ArenaSlab {
            .pointer = value.pointer, .layout = layout, .cursor = layout.size, .large = true });
        prepare_fresh_allocation(layout);
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
        prepare_fresh_allocation(request);
        record_allocation(request);
        return Ok(Allocation { pointer, request.size });
    }

public:
    explicit RecyclingArena(usize slab_size = usize(64 * 1024), Upstream upstream = Upstream {})
        : upstream_(rstd::move(upstream)),
          metadata_(METADATA_SLAB_SIZE, allocator_ref(upstream_)),
          slabs_(),
          recycled_(metadata_.allocator()),
          slab_size_(slab_size < usize(1024) ? usize(1024) : slab_size),
          current_slab_(NO_SLAB),
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
                    prepare_fresh_allocation(layout);
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
        auto recycled = recycled_class(layout);
        debug_assert(recycled.is_some());
        auto& values = recycled_[recycled.unwrap_unchecked()].pointers;
        debug_assert(values.len() < values.capacity());
        values.push(rstd::move(pointer));
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

    auto stats() const noexcept -> RecyclingArenaStats {
        auto result                    = stats_;
        auto metadata                  = metadata_.stats();
        result.metadata_used_bytes     = metadata.used_bytes;
        result.metadata_reserved_bytes = metadata.reserved_bytes;
        result.metadata_blocks         = metadata.ordinary_slabs + metadata.large_slabs;
        return result;
    }
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

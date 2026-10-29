module;
#include <rstd/macro.hpp>

export module rstd.alloc:arena;
export import :alloc;
export import :vec;
import :range;

using namespace rstd::prelude;

using rstd::alloc::Allocation;
using rstd::alloc::Allocator;
using rstd::alloc::AllocError;
using rstd::alloc::Layout;
using rstd::result::Result;

namespace alloc
{

/// Reserved bytes include slab metadata; used bytes count requests since the last reset.
export struct ArenaStats {
    usize used_bytes;
    usize reserved_bytes;
    usize padding_bytes;
    usize ordinary_slabs;
    usize large_slabs;
    usize peak_used_bytes;
    usize metadata_bytes;
};

/// Data reserved bytes include slab_metadata_bytes; metadata_reserved_bytes are separate.
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
    usize slab_metadata_bytes;
};

struct ArenaSlab {
    ArenaSlab*         next;
    ArenaSlab*         available_next;
    byte*              pointer;
    Layout             layout;
    Layout             allocation_layout;
    BumpRangeAllocator ranges;
    bool               large;
};

template<typename Upstream>
auto AllocateArenaSlab(Upstream& upstream, Layout layout, bool large)
    -> Result<ArenaSlab*, AllocError> {
    usize offset;
    auto  allocation_layout = Layout::make<ArenaSlab>().extend(layout, offset);
    if (allocation_layout.is_none()) return Err(AllocError {});
    auto allocation = as<Allocator>(upstream).allocate(*allocation_layout);
    if (allocation.is_err()) return Err(AllocError {});
    auto* base = static_cast<byte*>(allocation->pointer);
    return Ok(
        rstd::construct_at(reinterpret_cast<ArenaSlab*>(base),
                           ArenaSlab { .next              = nullptr,
                                       .available_next    = nullptr,
                                       .pointer           = base + offset.to_primitive(),
                                       .layout            = layout,
                                       .allocation_layout = *allocation_layout,
                                       .ranges = BumpRangeAllocator(layout.size.to_primitive()),
                                       .large  = large }));
}

template<typename Upstream>
void ReleaseArenaSlab(Upstream& upstream, ArenaSlab* slab) noexcept {
    auto layout = slab->allocation_layout;
    rstd::destroy_at(slab);
    as<Allocator>(upstream).deallocate(slab, layout);
}

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

export class ArenaReset {
    usize retain_limit_;

    constexpr explicit ArenaReset(usize retain_limit) noexcept: retain_limit_(retain_limit) {}

    template<typename>
    friend class BumpArena;

public:
    static constexpr auto free_all() noexcept -> ArenaReset { return ArenaReset(usize()); }
    static constexpr auto retain_capacity() noexcept -> ArenaReset {
        return ArenaReset(usize::MAX);
    }
    static constexpr auto retain_with_limit(usize bytes) noexcept -> ArenaReset {
        return ArenaReset(bytes);
    }
};

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

    RSTD_ATTR_NO_UNIQUE_ADDRESS Upstream upstream_;
    ArenaSlab*                           slabs_ {};
    ArenaSlab*                           current_slab_ {};
    ArenaSlab*                           available_ {};
    usize                                slab_size_;
    ArenaStats                           stats_ {};

    auto try_allocate(ArenaSlab& slab, Layout layout) noexcept -> void* {
        const auto before = slab.ranges.statistics().occupied_bytes;
        auto result = slab.ranges.allocate(layout.size.to_primitive(), layout.align.to_primitive());
        if (result.is_err()) return nullptr;
        const auto range   = result.unwrap_unchecked();
        auto       padding = usize(range.offset - before);
        stats_.used_bytes += layout.size;
        stats_.padding_bytes += padding;
        if (stats_.used_bytes > stats_.peak_used_bytes) {
            stats_.peak_used_bytes = stats_.used_bytes;
        }
        return slab.pointer + range.offset;
    }

    auto allocate_large(Layout layout) -> Result<Allocation, AllocError> {
        auto allocation = AllocateArenaSlab(upstream_, layout, true);
        if (allocation.is_err()) return Err(AllocError {});
        auto* slab = *allocation;
        slab->next = slabs_;
        slabs_     = slab;
        stats_.used_bytes += layout.size;
        stats_.reserved_bytes += slab->allocation_layout.size;
        stats_.metadata_bytes += slab->allocation_layout.size - layout.size;
        ++stats_.large_slabs;
        if (stats_.used_bytes > stats_.peak_used_bytes) {
            stats_.peak_used_bytes = stats_.used_bytes;
        }
        return Ok(Allocation { slab->pointer, layout.size });
    }

    auto add_ordinary_slab(Layout request) -> Result<Allocation, AllocError> {
        auto const alignment = request.align > usize(16) ? request.align : usize(16);
        auto const candidate = Layout::from_size_align(slab_size_, alignment);
        if (candidate.is_none()) return Err(AllocError {});
        auto const layout     = *candidate;
        auto       allocation = AllocateArenaSlab(upstream_, layout, false);
        if (allocation.is_err()) return Err(AllocError {});
        auto* slab = *allocation;
        slab->next = slabs_;
        slabs_ = current_slab_ = slab;
        stats_.reserved_bytes += slab->allocation_layout.size;
        stats_.metadata_bytes += slab->allocation_layout.size - layout.size;
        ++stats_.ordinary_slabs;
        auto pointer = try_allocate(*slab, request);
        debug_assert(pointer != nullptr);
        return Ok(Allocation { pointer, request.size });
    }

public:
    explicit BumpArena(usize slab_size = DEFAULT_SLAB_SIZE, Upstream upstream = Upstream {})
        : upstream_(rstd::move(upstream)),
          slab_size_(slab_size < usize(1024) ? usize(1024) : slab_size) {}

    BumpArena(const BumpArena&)                    = delete;
    auto operator=(const BumpArena&) -> BumpArena& = delete;
    BumpArena(BumpArena&&)                         = delete;
    auto operator=(BumpArena&&) -> BumpArena&      = delete;

    ~BumpArena() { reset(); }

    auto allocate(Layout layout) -> Result<Allocation, AllocError> {
        if (layout.size == usize()) {
            return Ok(Allocation { layout.dangling(), usize() });
        }

        if (layout.size > slab_size_ / usize(2) || layout.align > slab_size_ / usize(2)) {
            return allocate_large(layout);
        }

        if (current_slab_) {
            auto& slab = *current_slab_;
            if (layout.align <= slab.layout.align) {
                if (auto* pointer = try_allocate(slab, layout); pointer != nullptr) {
                    return Ok(Allocation { pointer, layout.size });
                }
            }
        }
        auto** available = &available_;
        while (*available) {
            auto* slab = *available;
            if (layout.align <= slab->layout.align) {
                if (auto* pointer = try_allocate(*slab, layout); pointer != nullptr) {
                    *available           = slab->available_next;
                    slab->available_next = nullptr;
                    current_slab_        = slab;
                    return Ok(Allocation { pointer, layout.size });
                }
            }
            available = &slab->available_next;
        }
        return add_ordinary_slab(layout);
    }

    /// All objects and borrows from previous allocations must have ended before reset.
    /// Retention counts complete upstream requests; dedicated slabs are always released.
    /// Peak usage is preserved across resets.
    void reset(ArenaReset policy = ArenaReset::free_all()) noexcept {
        auto* previous = slabs_;
        slabs_ = current_slab_ = available_ = nullptr;
        auto peak                           = stats_.peak_used_bytes;
        stats_                              = ArenaStats {};
        stats_.peak_used_bytes              = peak;
        auto** retained                     = &slabs_;
        while (previous) {
            auto* slab = previous;
            previous   = previous->next;
            if (slab->large ||
                slab->allocation_layout.size > policy.retain_limit_ - stats_.reserved_bytes) {
                ReleaseArenaSlab(upstream_, slab);
                continue;
            }
            slab->ranges         = BumpRangeAllocator(slab->layout.size.to_primitive());
            slab->next           = nullptr;
            *retained            = slab;
            retained             = &slab->next;
            slab->available_next = available_;
            available_           = slab;
            stats_.reserved_bytes += slab->allocation_layout.size;
            stats_.metadata_bytes += slab->allocation_layout.size - slab->layout.size;
            ++stats_.ordinary_slabs;
        }
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
    static constexpr usize METADATA_SLAB_SIZE = usize(64 * 1024);

    using MetadataUpstream  = rstd::ref<rstd::dyn<Allocator>>;
    using MetadataArena     = BumpArena<MetadataUpstream>;
    using MetadataAllocator = ArenaAllocator<MetadataUpstream>;
    using LayoutClass       = RecycledLayoutClass<MetadataAllocator>;

    RSTD_ATTR_NO_UNIQUE_ADDRESS Upstream            upstream_;
    MetadataArena                                   metadata_;
    ArenaSlab*                                      slabs_ {};
    alloc::vec::Vec<LayoutClass, MetadataAllocator> recycled_;
    usize                                           slab_size_;
    ArenaSlab*                                      current_slab_ {};
    RecyclingArenaStats                             stats_ {};

    auto try_allocate(ArenaSlab& slab, Layout layout) noexcept -> void* {
        auto result = slab.ranges.allocate(layout.size.to_primitive(), layout.align.to_primitive());
        if (result.is_err()) return nullptr;
        return slab.pointer + result.unwrap_unchecked().offset;
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

    auto prepare_fresh_allocation(Layout layout) -> Result<usize, AllocError> {
        auto found = recycled_class(layout);
        if (found.is_none()) {
            auto value = LayoutClass {
                .layout = layout,
                .pointers =
                    alloc::vec::Vec<void*, MetadataAllocator>::make_in(metadata_.allocator()),
                .physical_allocations = usize {},
            };
            if (value.pointers.try_reserve(usize(1)).is_err()) return Err(AllocError {});
            if (recycled_.try_reserve(usize(1)).is_err()) return Err(AllocError {});
            stats_.recycled_capacity += value.pointers.capacity();
            recycled_.push(rstd::move(value));
            found                 = Some(recycled_.len() - usize(1));
            stats_.layout_classes = recycled_.len();
            return Ok(*found);
        }
        auto& recycled = recycled_[*found];
        auto  required = recycled.physical_allocations.checked_add(usize(1));
        if (required.is_none()) return Err(AllocError {});
        if (recycled.pointers.capacity() < *required) {
            auto previous = recycled.pointers.capacity();
            if (recycled.pointers.try_reserve(*required - recycled.pointers.len()).is_err())
                return Err(AllocError {});
            stats_.recycled_capacity += recycled.pointers.capacity() - previous;
        }
        return Ok(*found);
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

public:
    explicit RecyclingArena(usize slab_size = usize(64 * 1024), Upstream upstream = Upstream {})
        : upstream_(rstd::move(upstream)),
          metadata_(METADATA_SLAB_SIZE, allocator_ref(upstream_)),
          recycled_(metadata_.allocator()),
          slab_size_(slab_size < usize(1024) ? usize(1024) : slab_size) {}

    RecyclingArena(const RecyclingArena&)                    = delete;
    auto operator=(const RecyclingArena&) -> RecyclingArena& = delete;
    RecyclingArena(RecyclingArena&&)                         = delete;
    auto operator=(RecyclingArena&&) -> RecyclingArena&      = delete;

    ~RecyclingArena() {
        while (slabs_) {
            auto* slab = slabs_;
            slabs_     = slab->next;
            ReleaseArenaSlab(upstream_, slab);
        }
    }

    auto allocate(Layout layout) -> Result<Allocation, AllocError> {
        if (layout.size == usize()) {
            return Ok(Allocation { layout.dangling(), usize() });
        }
        auto recycled = reuse(layout);
        if (recycled.is_some()) return Ok(rstd::move(recycled).unwrap());
        auto  large = layout.size > slab_size_ / usize(2) || layout.align > slab_size_ / usize(2);
        auto* slab  = large ? nullptr : current_slab_;
        if (slab) {
            auto preview = slab->ranges;
            if (layout.align > slab->layout.align ||
                preview.allocate(layout.size.to_primitive(), layout.align.to_primitive())
                    .is_err()) {
                slab = nullptr;
            }
        }
        auto fresh = slab == nullptr;
        if (fresh) {
            auto payload_layout = layout;
            if (! large) {
                auto alignment = layout.align.max(usize(16));
                auto ordinary  = Layout::from_size_align(slab_size_, alignment);
                if (ordinary.is_none()) return Err(AllocError {});
                payload_layout = *ordinary;
            }
            auto allocation = AllocateArenaSlab(upstream_, payload_layout, large);
            if (allocation.is_err()) return Err(AllocError {});
            slab = *allocation;
        }
        auto prepared = prepare_fresh_allocation(layout);
        if (prepared.is_err()) {
            if (fresh) ReleaseArenaSlab(upstream_, slab);
            return Err(AllocError {});
        }
        if (fresh) {
            slab->next = slabs_;
            slabs_     = slab;
            stats_.reserved_bytes += slab->allocation_layout.size;
            stats_.slab_metadata_bytes += slab->allocation_layout.size - slab->layout.size;
            if (large) {
                ++stats_.large_slabs;
            } else {
                ++stats_.ordinary_slabs;
                current_slab_ = slab;
            }
        }
        auto* pointer = large ? slab->pointer : try_allocate(*slab, layout);
        debug_assert(pointer != nullptr);
        ++recycled_[*prepared].physical_allocations;
        record_allocation(layout);
        return Ok(Allocation { pointer, layout.size });
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

namespace rstd
{
template<typename A>
struct Impl<alloc::AllocatorClone, ::alloc::ArenaAllocator<A>> {};
template<typename A>
struct Impl<alloc::AllocatorClone, ::alloc::RecyclingArenaAllocator<A>> {};
} // namespace rstd

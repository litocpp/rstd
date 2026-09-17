export module rstd.alloc:range_arena;
export import :range;

using namespace rstd::prelude;

export namespace alloc
{
template<typename Upstream, typename Metadata>
class RangeArena;

template<typename Upstream = Global, typename Metadata = Global>
class RangeArenaAllocator {
    RangeArena<Upstream, Metadata>* arena_;

public:
    explicit RangeArenaAllocator(RangeArena<Upstream, Metadata>& arena): arena_(&arena) {}
    auto arena() const noexcept -> RangeArena<Upstream, Metadata>& { return *arena_; }
};

/// Owns a fixed-capacity CPU block. Borrowed allocators must not outlive the arena.
template<typename Upstream = Global, typename Metadata = Global>
class RangeArena {
    struct Entry {
        RangeId             id;
        void*               pointer;
        rstd::alloc::Layout layout;
    };
    Upstream upstream_;
    Metadata metadata_;
    using MetadataRef = rstd::ref<rstd::dyn<rstd::alloc::Allocator>>;
    RangeAllocator<MetadataRef>  ranges_;
    vec::Vec<Entry, MetadataRef> entries_;
    rstd::alloc::Layout          block_layout_;
    void*                        base_ {};

public:
    explicit RangeArena(rstd::alloc::Layout capacity,
                        Upstream            upstream = Upstream {},
                        Metadata            metadata = Metadata {})
        : upstream_(rstd::move(upstream)),
          metadata_(rstd::move(metadata)),
          ranges_(capacity.size.to_primitive(), allocator_ref(metadata_)),
          entries_(vec::Vec<Entry, MetadataRef>::new_in(allocator_ref(metadata_))),
          block_layout_(capacity) {}
    RangeArena(const RangeArena&)            = delete;
    RangeArena& operator=(const RangeArena&) = delete;
    ~RangeArena() {
        if (base_) rstd::as<rstd::alloc::Allocator>(upstream_).deallocate(base_, block_layout_);
    }
    auto allocate(rstd::alloc::Layout layout)
        -> Result<rstd::alloc::Allocation, rstd::alloc::AllocError> {
        if (layout.size == usize())
            return Ok(rstd::alloc::Allocation { layout.dangling(), usize() });
        if (layout.align > block_layout_.align ||
            ! ValidRangeLayout(layout.size.to_primitive(), layout.align.to_primitive()))
            return Err(rstd::alloc::AllocError {});
        if (entries_.try_reserve(usize(1)).is_err()) return Err(rstd::alloc::AllocError {});
        if (! base_) {
            auto block = rstd::as<rstd::alloc::Allocator>(upstream_).allocate(block_layout_);
            if (block.is_err()) return Err(rstd::alloc::AllocError {});
            base_ = block.unwrap_unchecked().pointer;
        }
        auto allocation = ranges_.allocate(layout.size.to_primitive(), layout.align.to_primitive());
        if (allocation.is_err()) return Err(rstd::alloc::AllocError {});
        auto range   = allocation.unwrap_unchecked();
        auto pointer = static_cast<rstd::uint8_t*>(base_) + range.offset;
        entries_.push(Entry { range.id, pointer, layout });
        return Ok(rstd::alloc::Allocation { pointer, layout.size });
    }
    void deallocate(void* pointer, rstd::alloc::Layout layout) noexcept {
        if (layout.size == usize()) return;
        for (usize i {}; i < entries_.len(); ++i) {
            if (entries_[i].pointer != pointer) continue;
            auto result = ranges_.deallocate(entries_[i].id);
            if (result.is_err()) rstd::panic { "invalid arena allocation" };
            entries_.remove(i);
            return;
        }
        rstd::panic { "allocation does not belong to arena" };
    }
    auto allocator() noexcept -> RangeArenaAllocator<Upstream, Metadata> {
        return RangeArenaAllocator<Upstream, Metadata>(*this);
    }
    auto statistics() const noexcept -> RangeStatistics {
        auto result = ranges_.statistics();
        result.metadata_bytes += sizeof(*this) - sizeof(ranges_) +
                                 RangeSize(entries_.capacity().to_primitive()) * sizeof(Entry);
        return result;
    }
};
} // namespace alloc

namespace rstd
{
template<typename Upstream, typename Metadata>
struct Impl<alloc::Allocator, ::alloc::RangeArenaAllocator<Upstream, Metadata>>
    : DefaultInImpl<alloc::Allocator, ::alloc::RangeArenaAllocator<Upstream, Metadata>> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        return this->self().arena().allocate(layout);
    }
    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        this->self().arena().deallocate(pointer, layout);
    }
};
} // namespace rstd

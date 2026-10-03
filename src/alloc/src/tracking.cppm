export module rstd.alloc:tracking;
export import :alloc;

using namespace rstd::prelude;

export namespace rstd::alloc
{

struct AllocationStats {
    u64  allocations;
    u64  deallocations;
    u64  grows;
    u64  shrinks;
    u64  failures;
    u64  requested_bytes;
    u64  live_bytes;
    u64  peak_live_bytes;
    bool overflow {};
};

struct AllocationWindow {
    AllocationStats before;
    AllocationStats after;
    u64             peak_live_bytes;
};

enum class TrackingError
{
    WindowActive,
    NoWindow,
    InvalidAccounting
};

template<typename A>
class TrackingAllocator;

/// Thread-confined accounting; must outlive every allocator and allocation using it.
class AllocationTracker {
    template<typename A>
    friend class TrackingAllocator;
    AllocationStats stats_;
    AllocationStats baseline_;
    u64             window_peak_;
    bool            active_ {};

    void add(u64& target, u64 amount) noexcept {
        auto result = target.checked_add(amount);
        if (result.is_none()) {
            stats_.overflow = true;
            target          = u64::MAX;
        } else
            target = *result;
    }
    void release(usize size) noexcept {
        auto result = stats_.live_bytes.checked_sub(u64(size.to_primitive()));
        if (result.is_none())
            stats_.overflow = true;
        else
            stats_.live_bytes = *result;
    }
    void acquire(usize size) noexcept {
        add(stats_.live_bytes, u64(size.to_primitive()));
        if (stats_.live_bytes > stats_.peak_live_bytes) stats_.peak_live_bytes = stats_.live_bytes;
        if (active_ && stats_.live_bytes > window_peak_) window_peak_ = stats_.live_bytes;
    }

public:
    AllocationTracker()                                            = default;
    AllocationTracker(const AllocationTracker&)                    = delete;
    auto operator=(const AllocationTracker&) -> AllocationTracker& = delete;
    auto snapshot() const noexcept -> AllocationStats { return stats_; }

    auto begin_window() noexcept -> Result<empty, TrackingError> {
        if (active_) return Err(TrackingError::WindowActive);
        if (stats_.overflow) return Err(TrackingError::InvalidAccounting);
        baseline_    = stats_;
        window_peak_ = stats_.live_bytes;
        active_      = true;
        return Ok(empty {});
    }
    auto end_window() noexcept -> Result<AllocationWindow, TrackingError> {
        if (! active_) return Err(TrackingError::NoWindow);
        active_ = false;
        if (stats_.overflow) return Err(TrackingError::InvalidAccounting);
        return Ok(AllocationWindow { baseline_, stats_, window_peak_ });
    }
};

/// Counts logical layout bytes, not RSS or temporary allocations inside the underlying allocator.
template<typename A = ::alloc::Global>
class TrackingAllocator {
    static_assert(Impled<A, Allocator>);
    A                  allocator_;
    AllocationTracker* tracker_;

    auto record(Result<Allocation, AllocError> result, usize old_size, usize new_size) const
        -> Result<Allocation, AllocError> {
        if (result.is_err())
            tracker_->add(tracker_->stats_.failures, u64(1));
        else {
            tracker_->release(old_size);
            tracker_->acquire(new_size);
        }
        return result;
    }

public:
    TrackingAllocator(A allocator, AllocationTracker& tracker [[clang::lifetimebound]]
                      )
        : allocator_(rstd::move(allocator)), tracker_(&tracker) {}

    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        tracker_->add(tracker_->stats_.allocations, u64(1));
        tracker_->add(tracker_->stats_.requested_bytes, u64(layout.size.to_primitive()));
        return record(as<Allocator>(allocator_).allocate(layout), usize(), layout.size);
    }
    auto allocate_zeroed(Layout layout) const -> Result<Allocation, AllocError> {
        tracker_->add(tracker_->stats_.allocations, u64(1));
        tracker_->add(tracker_->stats_.requested_bytes, u64(layout.size.to_primitive()));
        return record(as<Allocator>(allocator_).allocate_zeroed(layout), usize(), layout.size);
    }
    void deallocate(void* pointer, Layout layout) const noexcept {
        as<Allocator>(allocator_).deallocate(pointer, layout);
        tracker_->add(tracker_->stats_.deallocations, u64(1));
        tracker_->release(layout.size);
    }
    auto grow(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        tracker_->add(tracker_->stats_.grows, u64(1));
        tracker_->add(tracker_->stats_.requested_bytes, u64(new_layout.size.to_primitive()));
        return record(as<Allocator>(allocator_).grow(pointer, old_layout, new_layout),
                      old_layout.size,
                      new_layout.size);
    }
    auto grow_zeroed(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        tracker_->add(tracker_->stats_.grows, u64(1));
        tracker_->add(tracker_->stats_.requested_bytes, u64(new_layout.size.to_primitive()));
        return record(as<Allocator>(allocator_).grow_zeroed(pointer, old_layout, new_layout),
                      old_layout.size,
                      new_layout.size);
    }
    auto shrink(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        tracker_->add(tracker_->stats_.shrinks, u64(1));
        tracker_->add(tracker_->stats_.requested_bytes, u64(new_layout.size.to_primitive()));
        return record(as<Allocator>(allocator_).shrink(pointer, old_layout, new_layout),
                      old_layout.size,
                      new_layout.size);
    }
};

} // namespace rstd::alloc

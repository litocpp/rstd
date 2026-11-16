export module rstd:async.frame_pool;
import rstd.alloc;
import :alloc.virtual_memory;
import :sync.mutex;
import :thread;
import :time;

using namespace rstd::prelude;
using rstd::u64;
using rstd::alloc::Allocation;
using rstd::alloc::Allocator;
using rstd::alloc::AllocError;
using rstd::alloc::Layout;
using rstd::alloc::PoolAllocator;
using rstd::alloc::PoolOptions;
using rstd::alloc::PoolSizeClasses;
using rstd::alloc::PoolStatistics;
using rstd::alloc::PoolTrimReport;
using rstd::alloc::VirtualMemoryAllocator;
using rstd::alloc::VirtualMemoryStatistics;
using ::alloc::sync::Arc;
using rstd::sync::Mutex;
using rstd::thread::ThreadId;
using rstd::time::Duration;
using rstd::time::Instant;

namespace rstd::async
{

export struct FrameAllocatorOptions {
    PoolOptions pool { .size_classes = PoolSizeClasses::Fine };
    // Fixed for the allocation domain; enabling records adds per-block storage and clock reads.
    bool record_allocations {};
};

export struct FrameAllocationStatistics {
    bool  enabled {};
    usize allocations {}, deallocations {}, failures {};
    usize live_allocations {}, peak_live_allocations {}, requested_bytes {};
    usize live_bytes {}, peak_live_bytes {}, record_bytes {}, remote_deallocations {};
    // Completed allocation lifetimes: <1ms, <10ms, <100ms, <1s, <10s, <60s, <600s, >=600s.
    array<usize, 8> lifetime_counts {};
};

export struct FrameSizeStatistics {
    usize capacity {}, allocations {}, deallocations {};
    usize live_allocations {}, peak_live_allocations {}, live_bytes {}, peak_live_bytes {};
};

export struct FrameAllocatorSnapshot {
    PoolStatistics            pool;
    VirtualMemoryStatistics   upstream;
    FrameAllocationStatistics records;
    // Classes describe the pool input, including diagnostic storage; the last is dedicated.
    array<FrameSizeStatistics, 65> classes;
    usize                          control_bytes;
};

struct FrameAllocationRecord {
    Instant  created;
    ThreadId thread;
};

class FrameAllocatorState {
public:
    PoolAllocator<VirtualMemoryAllocator> pool;
    FrameAllocationStatistics             records;
    array<FrameSizeStatistics, 65>        classes {};
    usize                                 control_bytes;

    explicit FrameAllocatorState(FrameAllocatorOptions options, usize control_bytes)
        : pool(options.pool, VirtualMemoryAllocator {}), control_bytes(control_bytes) {
        records.enabled = options.record_allocations;
    }

    auto recorded_layout(Layout input, usize& offset) const -> Option<Layout> {
        return Layout::make<FrameAllocationRecord>().extend(input, offset);
    }

    auto size_index(Layout input) const -> usize {
        auto bucket = pool.classify(input);
        return bucket.is_some() ? bucket->index : usize(64);
    }

    auto allocate(Layout input) -> Result<Allocation, AllocError> {
        if (input.size() == usize()) return Ok(Allocation { input.dangling(), usize() });
        if (! records.enabled) return pool.allocate(input);

        usize offset {};
        auto  layout = recorded_layout(input, offset);
        if (layout.is_none()) {
            ++records.failures;
            return Err(AllocError {});
        }
        auto allocation = pool.allocate(*layout);
        if (allocation.is_err()) {
            ++records.failures;
            return Err(AllocError {});
        }
        auto* base = static_cast<byte*>(allocation->pointer);
        rstd::construct_at(reinterpret_cast<FrameAllocationRecord*>(base),
                           FrameAllocationRecord { Instant::now(), thread::current_id() });
        ++records.allocations;
        ++records.live_allocations;
        records.peak_live_allocations = records.peak_live_allocations.max(records.live_allocations);
        records.requested_bytes += input.size();
        records.live_bytes += input.size();
        records.peak_live_bytes = records.peak_live_bytes.max(records.live_bytes);
        records.record_bytes += offset;

        auto& bucket = classes[size_index(*layout)];
        if (auto classification = pool.classify(*layout); classification.is_some())
            bucket.capacity = classification->capacity;
        ++bucket.allocations;
        ++bucket.live_allocations;
        bucket.peak_live_allocations = bucket.peak_live_allocations.max(bucket.live_allocations);
        bucket.live_bytes += input.size();
        bucket.peak_live_bytes = bucket.peak_live_bytes.max(bucket.live_bytes);
        return Ok(Allocation { base + offset.to_primitive(), input.size() });
    }

    void deallocate(void* pointer, Layout input) noexcept {
        if (input.size() == usize()) return;
        if (! records.enabled) {
            pool.deallocate(pointer, input);
            return;
        }
        usize offset {};
        auto  layout = recorded_layout(input, offset).unwrap_unchecked();
        auto* base   = static_cast<byte*>(pointer) - offset.to_primitive();
        auto* record = reinterpret_cast<FrameAllocationRecord*>(base);
        auto  age    = record->created.elapsed();
        if (record->thread != thread::current_id()) ++records.remote_deallocations;
        constexpr unsigned limits[] { 1, 10, 100, 1000, 10000, 60000, 600000 };
        usize              lifetime;
        while (lifetime < usize(7) &&
               age >= Duration::from_millis(u64(limits[lifetime.to_primitive()])))
            ++lifetime;
        ++records.lifetime_counts[lifetime];
        ++records.deallocations;
        --records.live_allocations;
        records.live_bytes -= input.size();
        records.record_bytes -= offset;
        auto& bucket = classes[size_index(layout)];
        ++bucket.deallocations;
        --bucket.live_allocations;
        bucket.live_bytes -= input.size();
        rstd::destroy_at(record);
        pool.deallocate(base, layout);
    }
};

/// Owns the shared pool; borrowed frame allocator references must not outlive its last owner.
export class FrameAllocator {
    using State = Mutex<FrameAllocatorState>;
    Arc<State> state_;

    explicit FrameAllocator(Arc<State> state): state_(rstd::move(state)) {}

public:
    FrameAllocator(const FrameAllocator&)                        = delete;
    FrameAllocator(FrameAllocator&&) noexcept                    = default;
    auto operator=(FrameAllocator&&) noexcept -> FrameAllocator& = default;

    static auto try_make(FrameAllocatorOptions options = {}) -> Result<FrameAllocator, AllocError> {
        auto control = VirtualMemoryAllocator {};
        auto layout  = Arc<State>::template allocation_layout_in<VirtualMemoryAllocator>();
        if (layout.is_none()) return Err(AllocError {});
        auto bytes = control.allocation_size(*layout);
        if (bytes.is_none()) return Err(AllocError {});
        auto state = Arc<State>::try_make_in(rstd::move(control), options, *bytes);
        if (state.is_err()) return Err(AllocError {});
        return Ok(FrameAllocator(state.unwrap_unchecked()));
    }

    auto clone() const -> FrameAllocator { return FrameAllocator(state_.clone()); }

    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        return state_->lock().unwrap_unchecked()->allocate(layout);
    }

    auto allocate_zeroed(Layout layout) const -> Result<Allocation, AllocError> {
        auto result = allocate(layout);
        if (result.is_ok() && layout.size() != usize())
            rstd::mem::memset(result->pointer, u8(), layout.size());
        return result;
    }

    void deallocate(void* pointer, Layout layout) const noexcept {
        state_->lock().unwrap_unchecked()->deallocate(pointer, layout);
    }

    auto trim(usize retain_bytes = {}) const -> PoolTrimReport {
        return state_->lock().unwrap_unchecked()->pool.trim(retain_bytes);
    }

    auto snapshot() const -> FrameAllocatorSnapshot {
        auto state = state_->lock().unwrap_unchecked();
        return { state->pool.statistics(),
                 state->pool.upstream_statistics(),
                 state->records,
                 state->classes,
                 state->control_bytes };
    }
};

} // namespace rstd::async

namespace rstd
{
template<>
struct Impl<alloc::Allocator, async::FrameAllocator>
    : DefaultInImpl<alloc::Allocator, async::FrameAllocator> {
    auto allocate(alloc::Layout layout) const { return this->self().allocate(layout); }
    auto allocate_zeroed(alloc::Layout layout) const {
        return this->self().allocate_zeroed(layout);
    }
    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        this->self().deallocate(pointer, layout);
    }
};
template<>
struct Impl<alloc::AllocatorClone, async::FrameAllocator> {};
template<>
struct Impl<alloc::ThreadSafeAllocator, async::FrameAllocator> {};
template<>
struct Impl<clone::Clone, async::FrameAllocator>
    : DefaultInImpl<clone::Clone, async::FrameAllocator> {
    auto clone() const -> async::FrameAllocator { return this->self().clone(); }
};
} // namespace rstd

export module rstd:alloc.shared_tracking;
import rstd.alloc;
import :sync.mutex;

using namespace rstd::prelude;

export namespace rstd::alloc
{

/// Serializes upstream operations and logical accounting. Callbacks must not reenter this owner.
/// State uses Global independently of the managed allocations. Borrowed upstreams stay borrowed.
template<typename A = ::alloc::Global>
class SharedTrackingAllocator {
    static_assert(Impled<A, Allocator>);
    struct State {
        A                 upstream;
        AllocationTracker tracker;
        explicit State(A allocator): upstream(rstd::move(allocator)) {}
    };
    using LockedState = rstd::sync::Mutex<State>;
    using Shared      = ::alloc::sync::Arc<LockedState>;
    Shared state_;
    explicit SharedTrackingAllocator(Shared state): state_(rstd::move(state)) {}

public:
    class Observer {
        ::alloc::sync::Weak<LockedState> state_;
        explicit Observer(::alloc::sync::Weak<LockedState> state): state_(rstd::move(state)) {}
        friend class SharedTrackingAllocator;

    public:
        auto clone() const -> Observer { return Observer(state_.clone()); }
        auto expired() const noexcept -> bool { return state_.expired(); }
        auto snapshot() const -> Option<AllocationStats> {
            auto owner = state_.upgrade();
            if (! owner) return None();
            auto state = owner->lock().unwrap_unchecked();
            return Some(state->tracker.snapshot());
        }
    };

    SharedTrackingAllocator(const SharedTrackingAllocator&)                        = delete;
    auto operator=(const SharedTrackingAllocator&) -> SharedTrackingAllocator&     = delete;
    SharedTrackingAllocator(SharedTrackingAllocator&&) noexcept                    = default;
    auto operator=(SharedTrackingAllocator&&) noexcept -> SharedTrackingAllocator& = default;

    static auto try_make(A upstream = A {}) -> Result<SharedTrackingAllocator, AllocError> {
        auto state = Shared::try_make(rstd::move(upstream));
        if (state.is_err()) return Err(AllocError {});
        return Ok(SharedTrackingAllocator(state.unwrap_unchecked()));
    }
    static auto make(A upstream = A {}) -> SharedTrackingAllocator {
        auto result = try_make(rstd::move(upstream));
        if (result.is_err()) rstd::panic { "shared allocator state allocation failed" };
        return result.unwrap_unchecked();
    }
    auto clone() const -> SharedTrackingAllocator {
        return SharedTrackingAllocator(state_.clone());
    }
    auto downgrade() const -> Observer { return Observer(state_.downgrade()); }
    auto snapshot() const -> AllocationStats {
        auto state = state_->lock().unwrap_unchecked();
        return state->tracker.snapshot();
    }
    template<typename U = A>
    auto upstream_statistics() const -> mtp::rm_cvf<decltype(mtp::declval<const U&>().statistics())>
        requires requires(const U& upstream) { upstream.statistics(); }
    {
        auto state = state_->lock().unwrap_unchecked();
        return state->upstream.statistics();
    }
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        auto state    = state_->lock().unwrap_unchecked();
        auto tracking = TrackingAllocator(::alloc::allocator_ref(state->upstream), state->tracker);
        return tracking.allocate(layout);
    }
    auto allocate_zeroed(Layout layout) const -> Result<Allocation, AllocError> {
        auto state    = state_->lock().unwrap_unchecked();
        auto tracking = TrackingAllocator(::alloc::allocator_ref(state->upstream), state->tracker);
        return tracking.allocate_zeroed(layout);
    }
    auto deallocate(void* pointer, Layout layout) const noexcept -> void {
        auto state    = state_->lock().unwrap_unchecked();
        auto tracking = TrackingAllocator(::alloc::allocator_ref(state->upstream), state->tracker);
        return tracking.deallocate(pointer, layout);
    }
    auto grow(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        auto state    = state_->lock().unwrap_unchecked();
        auto tracking = TrackingAllocator(::alloc::allocator_ref(state->upstream), state->tracker);
        return tracking.grow(pointer, old_layout, new_layout);
    }
    auto grow_zeroed(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        auto state    = state_->lock().unwrap_unchecked();
        auto tracking = TrackingAllocator(::alloc::allocator_ref(state->upstream), state->tracker);
        return tracking.grow_zeroed(pointer, old_layout, new_layout);
    }
    auto shrink(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        auto state    = state_->lock().unwrap_unchecked();
        auto tracking = TrackingAllocator(::alloc::allocator_ref(state->upstream), state->tracker);
        return tracking.shrink(pointer, old_layout, new_layout);
    }
};
} // namespace rstd::alloc
namespace rstd
{
template<typename A>
struct Impl<alloc::AllocatorClone, alloc::SharedTrackingAllocator<A>> {};
template<typename A>
    requires(Impled<A, alloc::ThreadSafeAllocator> || Impled<A, alloc::TransferableAllocator>)
struct Impl<alloc::ThreadSafeAllocator, alloc::SharedTrackingAllocator<A>> {};
} // namespace rstd

namespace rstd
{
template<typename A>
struct Impl<clone::Clone, alloc::SharedTrackingAllocator<A>>
    : DefaultInImpl<clone::Clone, alloc::SharedTrackingAllocator<A>> {
    auto clone() const -> alloc::SharedTrackingAllocator<A> { return this->self().clone(); }
};
} // namespace rstd

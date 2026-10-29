export module rstd:alloc.shared_pool;
import rstd.alloc;
import :sync.mutex;
using namespace rstd::prelude;

export namespace rstd::alloc
{
template<typename UpstreamStatistics>
struct SharedPoolStatistics {
    PoolStatistics     pool;
    UpstreamStatistics upstream;
};
template<typename Upstream = ::alloc::Global>
class SharedPoolAllocator {
    using State  = rstd::sync::Mutex<PoolAllocator<Upstream>>;
    using Shared = ::alloc::sync::Arc<State>;
    Shared state_;
    explicit SharedPoolAllocator(Shared state): state_(rstd::move(state)) {}

public:
    SharedPoolAllocator(SharedPoolAllocator&&) noexcept                    = default;
    auto operator=(SharedPoolAllocator&&) noexcept -> SharedPoolAllocator& = default;
    template<typename C>
    static auto try_make_in(C control, PoolOptions options = {}, Upstream upstream = {})
        -> Result<SharedPoolAllocator, AllocError> {
        auto state = Shared::try_make_in(rstd::move(control), options, rstd::move(upstream));
        if (state.is_err()) return Err(AllocError {});
        return Ok(SharedPoolAllocator(state.unwrap_unchecked()));
    }
    static auto try_make(PoolOptions options = {}, Upstream upstream = {})
        -> Result<SharedPoolAllocator, AllocError> {
        return try_make_in(::alloc::Global {}, options, rstd::move(upstream));
    }
    template<typename C>
    static auto control_layout() -> Option<Layout> {
        return Shared::template allocation_layout_in<C>();
    }
    auto clone() const -> SharedPoolAllocator { return SharedPoolAllocator(state_.clone()); }
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        return state_->lock().unwrap_unchecked()->allocate(layout);
    }
    auto grow(void* p, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        return state_->lock().unwrap_unchecked()->grow(p, old_layout, new_layout);
    }
    auto allocate_zeroed(Layout layout) const -> Result<Allocation, AllocError> {
        return state_->lock().unwrap_unchecked()->allocate_zeroed(layout);
    }
    void deallocate(void* pointer, Layout layout) const noexcept {
        state_->lock().unwrap_unchecked()->deallocate(pointer, layout);
    }
    auto trim(usize retain = {}) const -> PoolTrimReport {
        return state_->lock().unwrap_unchecked()->trim(retain);
    }
    auto statistics() const -> PoolStatistics {
        return state_->lock().unwrap_unchecked()->statistics();
    }
    template<typename U = Upstream>
    auto snapshot() const
        -> SharedPoolStatistics<mtp::rm_cvf<decltype(mtp::declval<const U&>().statistics())>>
        requires requires(const U& a) { a.statistics(); }
    {
        auto state = state_->lock().unwrap_unchecked();
        return { state->statistics(), state->upstream_statistics() };
    }
    auto upstream_statistics() const
        requires requires(const Upstream& a) { a.statistics(); }
    {
        return state_->lock().unwrap_unchecked()->upstream_statistics();
    }
};
} // namespace rstd::alloc
namespace rstd
{
template<typename A>
struct Impl<alloc::Allocator, alloc::SharedPoolAllocator<A>>
    : DefaultInImpl<alloc::Allocator, alloc::SharedPoolAllocator<A>> {
    auto allocate(alloc::Layout l) const { return this->self().allocate(l); }
    auto grow(void* p, alloc::Layout old_layout, alloc::Layout new_layout) const {
        return this->self().grow(p, old_layout, new_layout);
    }
    auto allocate_zeroed(alloc::Layout l) const { return this->self().allocate_zeroed(l); }
    void deallocate(void* p, alloc::Layout l) const noexcept { this->self().deallocate(p, l); }
};
template<typename A>
struct Impl<alloc::AllocatorClone, alloc::SharedPoolAllocator<A>> {};
template<typename A>
    requires(Impled<A, alloc::ThreadSafeAllocator> || Impled<A, alloc::TransferableAllocator>)
struct Impl<alloc::ThreadSafeAllocator, alloc::SharedPoolAllocator<A>> {};
template<typename A>
struct Impl<clone::Clone, alloc::SharedPoolAllocator<A>>
    : DefaultInImpl<clone::Clone, alloc::SharedPoolAllocator<A>> {
    auto clone() const -> alloc::SharedPoolAllocator<A> { return this->self().clone(); }
};
} // namespace rstd

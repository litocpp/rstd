export module rstd:alloc.budget;
import rstd.alloc;
import :sync.mutex;

using namespace rstd::prelude;

export namespace rstd::alloc
{

struct BudgetStatistics {
    usize limit;
    usize live_bytes;
    usize peak_bytes;
    usize allocations; // Successful allocation calls, including zero-sized layouts.
    usize failures;    // Budget rejections and upstream allocation failures.
};

/// Shares the upstream lifetime and serializes allocation, accounting and release.
/// Counts live requested layout bytes, excluding shared state, allocator metadata and page rounding.
/// Default grow/shrink count both allocations until the old allocation is released.
/// Allocations must be released through this allocator or a clone before the last handle is dropped.
template<typename Upstream = ::alloc::Global>
class BudgetAllocator {
    static_assert(Impled<Upstream, Allocator>);
    struct State {
        Upstream         upstream;
        BudgetStatistics statistics;

        State(usize limit, Upstream allocator)
            : upstream(rstd::move(allocator)), statistics { .limit = limit } {}
    };
    using Shared = ::alloc::sync::Arc<rstd::sync::Mutex<State>>;
    Shared state_;

    explicit BudgetAllocator(Shared state): state_(rstd::move(state)) {}

public:
    BudgetAllocator(const BudgetAllocator&)                        = delete;
    auto operator=(const BudgetAllocator&) -> BudgetAllocator&     = delete;
    BudgetAllocator(BudgetAllocator&&) noexcept                    = default;
    auto operator=(BudgetAllocator&&) noexcept -> BudgetAllocator& = default;

    static auto try_make(usize limit, Upstream upstream = Upstream {})
        -> Result<BudgetAllocator, AllocError> {
        auto state = Shared::try_make(limit, rstd::move(upstream));
        if (state.is_err()) return Err(AllocError {});
        return Ok(BudgetAllocator(state.unwrap_unchecked()));
    }

    auto clone() const -> BudgetAllocator { return BudgetAllocator(state_.clone()); }

    auto statistics() const -> BudgetStatistics {
        auto state = state_->lock().unwrap_unchecked();
        return state->statistics;
    }

    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        auto  state = state_->lock().unwrap_unchecked();
        auto& stats = state->statistics;
        if (layout.size > stats.limit - stats.live_bytes) {
            ++stats.failures;
            return Err(AllocError {});
        }
        auto result = as<Allocator>(state->upstream).allocate(layout);
        if (result.is_err()) {
            ++stats.failures;
            return result;
        }
        stats.live_bytes += layout.size;
        stats.peak_bytes = stats.peak_bytes.max(stats.live_bytes);
        ++stats.allocations;
        return result;
    }

    void deallocate(void* pointer, Layout layout) const noexcept {
        auto state = state_->lock().unwrap_unchecked();
        as<Allocator>(state->upstream).deallocate(pointer, layout);
        state->statistics.live_bytes -= layout.size;
    }
};

} // namespace rstd::alloc

namespace rstd
{
template<typename Upstream>
struct Impl<clone::Clone, alloc::BudgetAllocator<Upstream>>
    : DefaultInImpl<clone::Clone, alloc::BudgetAllocator<Upstream>> {
    auto clone() const -> alloc::BudgetAllocator<Upstream> { return this->self().clone(); }
};

template<typename Upstream>
struct Impl<alloc::Allocator, alloc::BudgetAllocator<Upstream>>
    : DefaultInImpl<alloc::Allocator, alloc::BudgetAllocator<Upstream>> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        return this->self().allocate(layout);
    }
    void deallocate(void* pointer, alloc::Layout layout) const noexcept {
        this->self().deallocate(pointer, layout);
    }
};
} // namespace rstd

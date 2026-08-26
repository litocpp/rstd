module;
#include <rstd/macro.hpp>

export module rstd:alloc.shared_arena;
import rstd.alloc;
import :sync.mutex;

using namespace rstd::prelude;
using rstd::alloc::Allocation;
using rstd::alloc::Allocator;
using rstd::alloc::AllocError;
using rstd::alloc::Layout;
using rstd::result::Result;

namespace rstd::alloc
{

export template<typename UpstreamStatistics>
struct SharedArenaStatistics {
    ::alloc::ArenaStats arena;
    UpstreamStatistics  upstream;
};

export template<typename Upstream>
class SharedBumpArena;

export template<typename Upstream>
class SharedArenaAllocator;

export template<typename Upstream>
class WeakSharedArena {
    using State = rstd::sync::Mutex<::alloc::BumpArena<Upstream>>;

    ::alloc::sync::Weak<State> state_;

    explicit WeakSharedArena(::alloc::sync::Weak<State> state): state_(rstd::move(state)) {}

    friend class SharedBumpArena<Upstream>;

public:
    WeakSharedArena(const WeakSharedArena&)                        = delete;
    auto operator=(const WeakSharedArena&) -> WeakSharedArena&     = delete;
    WeakSharedArena(WeakSharedArena&&) noexcept                    = default;
    auto operator=(WeakSharedArena&&) noexcept -> WeakSharedArena& = default;

    auto clone() const -> WeakSharedArena { return WeakSharedArena(state_.clone()); }

    auto expired() const noexcept -> bool { return state_.expired(); }
};

export template<typename Upstream>
class SharedArenaAllocator {
    using State = rstd::sync::Mutex<::alloc::BumpArena<Upstream>>;

    ::alloc::sync::Arc<State> state_;

    explicit SharedArenaAllocator(::alloc::sync::Arc<State> state): state_(rstd::move(state)) {}

    friend class SharedBumpArena<Upstream>;

public:
    SharedArenaAllocator(const SharedArenaAllocator&)                        = delete;
    auto operator=(const SharedArenaAllocator&) -> SharedArenaAllocator&     = delete;
    SharedArenaAllocator(SharedArenaAllocator&&) noexcept                    = default;
    auto operator=(SharedArenaAllocator&&) noexcept -> SharedArenaAllocator& = default;

    auto clone() const -> SharedArenaAllocator { return SharedArenaAllocator(state_.clone()); }

    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        auto arena = state_->lock().unwrap_unchecked();
        return arena->allocate(layout);
    }

    auto arena_statistics() const -> ::alloc::ArenaStats {
        auto arena = state_->lock().unwrap_unchecked();
        return arena->stats();
    }

    template<typename U = Upstream>
    auto statistics() const
        -> SharedArenaStatistics<mtp::rm_cvf<decltype(mtp::declval<const U&>().statistics())>>
        requires requires(const U& upstream) { upstream.statistics(); }
    {
        auto arena = state_->lock().unwrap_unchecked();
        return SharedArenaStatistics<mtp::rm_cvf<decltype(mtp::declval<const U&>().statistics())>> {
            .arena    = arena->stats(),
            .upstream = arena->upstream_statistics(),
        };
    }
};

export template<typename Upstream = ::alloc::Global>
class SharedBumpArena {
    static constexpr usize DEFAULT_SLAB_SIZE = usize(64 * 1024);
    using State                              = rstd::sync::Mutex<::alloc::BumpArena<Upstream>>;
    using SharedState                        = ::alloc::sync::Arc<State>;

    SharedState state_;

    explicit SharedBumpArena(SharedState state): state_(rstd::move(state)) {}

public:
    SharedBumpArena(const SharedBumpArena&)                        = delete;
    auto operator=(const SharedBumpArena&) -> SharedBumpArena&     = delete;
    SharedBumpArena(SharedBumpArena&&) noexcept                    = default;
    auto operator=(SharedBumpArena&&) noexcept -> SharedBumpArena& = default;

    static auto make(usize slab_size = DEFAULT_SLAB_SIZE, Upstream upstream = Upstream {})
        -> SharedBumpArena {
        return SharedBumpArena(SharedState::make(slab_size, rstd::move(upstream)));
    }

    auto allocator() const -> SharedArenaAllocator<Upstream> {
        return SharedArenaAllocator<Upstream>(state_.clone());
    }

    auto downgrade() const -> WeakSharedArena<Upstream> {
        return WeakSharedArena<Upstream>(state_.downgrade());
    }

    auto arena_statistics() const -> ::alloc::ArenaStats {
        auto arena = state_->lock().unwrap_unchecked();
        return arena->stats();
    }

    template<typename U = Upstream>
    auto statistics() const
        -> SharedArenaStatistics<mtp::rm_cvf<decltype(mtp::declval<const U&>().statistics())>>
        requires requires(const U& upstream) { upstream.statistics(); }
    {
        auto arena = state_->lock().unwrap_unchecked();
        return SharedArenaStatistics<mtp::rm_cvf<decltype(mtp::declval<const U&>().statistics())>> {
            .arena    = arena->stats(),
            .upstream = arena->upstream_statistics(),
        };
    }
};

} // namespace rstd::alloc

namespace rstd
{

template<typename Upstream>
struct Impl<alloc::Allocator, alloc::SharedArenaAllocator<Upstream>>
    : DefaultInImpl<alloc::Allocator, alloc::SharedArenaAllocator<Upstream>> {
    auto allocate(alloc::Layout layout) const -> Result<alloc::Allocation, alloc::AllocError> {
        return this->self().allocate(layout);
    }

    void deallocate(void*, alloc::Layout) const noexcept {}
};

} // namespace rstd

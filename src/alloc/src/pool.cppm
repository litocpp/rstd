module;
#include <rstd/macro.hpp>
export module rstd.alloc:pool;
import :alloc;
using namespace rstd::prelude;

export namespace rstd::alloc
{
struct PoolOptions {
    usize slab_bytes { 262144 }, max_pooled_block_bytes { 65536 }, retained_bytes { 1048576 };
};
struct PoolStatistics {
    usize live_bytes {}, live_block_bytes {}, peak_live_bytes {}, reserved_bytes {};
    usize retained_bytes {}, metadata_bytes {}, slabs {}, allocations {}, reuses {}, failures {};
    usize grows {}, trims {}, released_bytes {};
};
struct PoolTrimReport {
    usize bytes {}, slabs {};
};

template<typename Upstream = ::alloc::Global>
class PoolAllocator {
    struct Slab;
    struct alignas(16) Block {
        Slab*  slab;
        Block* next;
    };
    struct Slab {
        Slab * previous {}, *next {}, *available_previous {}, *available_next {};
        Block* free {};
        Layout layout;
        usize  index, stride, live {}, slots;
    };
    static constexpr unsigned classes = 13;
    Upstream                  upstream_;
    PoolOptions               options_;
    mutable PoolStatistics    stats_;
    mutable Slab*             slabs_ {};
    mutable Slab*             available_[classes] {};

    void make_available(Slab* slab) const {
        auto i               = slab->index.to_primitive();
        slab->available_next = available_[i];
        if (available_[i]) available_[i]->available_previous = slab;
        available_[i] = slab;
    }
    void remove_available(Slab* slab) const {
        if (slab->available_previous)
            slab->available_previous->available_next = slab->available_next;
        else
            available_[slab->index.to_primitive()] = slab->available_next;
        if (slab->available_next)
            slab->available_next->available_previous = slab->available_previous;
        slab->available_next = slab->available_previous = nullptr;
    }
    void release(Slab* slab) const noexcept {
        auto layout = slab->layout;
        if (slab->index != usize::MAX) {
            remove_available(slab);
            stats_.retained_bytes -= layout.size;
        }
        if (slab->previous)
            slab->previous->next = slab->next;
        else
            slabs_ = slab->next;
        if (slab->next) slab->next->previous = slab->previous;
        stats_.metadata_bytes -= usize(sizeof(Slab)) + slab->slots * usize(sizeof(Block));
        stats_.reserved_bytes -= layout.size;
        stats_.released_bytes += layout.size;
        --stats_.slabs;
        rstd::destroy_at(slab);
        as<Allocator>(upstream_).deallocate(slab, layout);
    }
    auto new_slab(usize index, usize payload, usize alignment) const -> Slab* {
        auto header = usize(sizeof(Slab)).checked_add(usize(sizeof(Block)));
        if (header.is_none()) return nullptr;
        auto offset = header->checked_add(alignment - usize(1));
        if (offset.is_none()) return nullptr;
        *offset   = (*offset / alignment) * alignment;
        auto size = offset->checked_add(payload);
        if (size.is_none()) return nullptr;
        bool pooled = index != usize::MAX;
        if (pooled) *size = size->max(options_.slab_bytes);
        auto layout = Layout::from_size_align(*size, alignment);
        if (layout.is_none()) return nullptr;
        auto memory = as<Allocator>(upstream_).allocate(*layout);
        if (memory.is_err()) return nullptr;
        auto* base   = static_cast<byte*>(memory->pointer);
        auto  stride = payload + usize(sizeof(Block));
        auto  slots  = pooled ? (*size - *offset + usize(sizeof(Block))) / stride : usize(1);
        auto* slab   = rstd::construct_at(reinterpret_cast<Slab*>(base),
                                          Slab { .next   = slabs_,
                                                 .layout = *layout,
                                                 .index  = index,
                                                 .stride = stride,
                                                 .slots  = slots });
        if (slabs_) slabs_->previous = slab;
        slabs_ = slab;
        for (usize i; i < slots; ++i) {
            auto* block = rstd::construct_at(
                reinterpret_cast<Block*>(
                    base + (*offset - usize(sizeof(Block)) + i * stride).to_primitive()),
                Block { slab, slab->free });
            slab->free = block;
        }
        stats_.reserved_bytes += *size;
        stats_.metadata_bytes += usize(sizeof(Slab)) + slots * usize(sizeof(Block));
        ++stats_.slabs;
        if (pooled) {
            stats_.retained_bytes += *size;
            make_available(slab);
        }
        return slab;
    }

public:
    explicit PoolAllocator(PoolOptions options = {}, Upstream upstream = {})
        : upstream_(rstd::move(upstream)), options_(options) {}
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&)      = delete;
    ~PoolAllocator() {
        debug_assert(stats_.live_bytes == usize());
        while (slabs_) release(slabs_);
    }
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        if (layout.size == usize()) return Ok(Allocation { layout.dangling(), usize() });
        usize payload(16), index;
        while (payload < layout.size && index < usize(classes - 1)) {
            payload *= usize(2);
            ++index;
        }
        bool  pooled = layout.align <= usize(16) && layout.size <= payload &&
                       payload <= options_.max_pooled_block_bytes;
        auto* slab   = pooled ? available_[index.to_primitive()] : nullptr;
        bool  reused = slab != nullptr;
        if (! slab)
            slab = new_slab(pooled ? index : usize::MAX,
                            pooled ? payload : layout.size,
                            layout.align.max(usize(16)));
        if (! slab) {
            ++stats_.failures;
            return Err(AllocError {});
        }
        if (pooled && slab->live == usize()) stats_.retained_bytes -= slab->layout.size;
        auto* block = slab->free;
        slab->free  = block->next;
        ++slab->live;
        if (pooled && ! slab->free) remove_available(slab);
        stats_.live_bytes += layout.size;
        stats_.live_block_bytes += slab->stride;
        stats_.peak_live_bytes = stats_.peak_live_bytes.max(stats_.live_bytes);
        ++stats_.allocations;
        if (reused) ++stats_.reuses;
        return Ok(Allocation { reinterpret_cast<byte*>(block) + sizeof(Block), layout.size });
    }
    auto grow(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size >= old_layout.size);
        ++stats_.grows;
        auto result = allocate(new_layout);
        if (result.is_err()) return result;
        if (old_layout.size != usize())
            rstd::mem::memcpy(result->pointer, pointer, old_layout.size);
        deallocate(pointer, old_layout);
        return result;
    }
    auto allocate_zeroed(Layout layout) const -> Result<Allocation, AllocError> {
        auto result = allocate(layout);
        if (result.is_ok() && layout.size != usize())
            rstd::mem::memset(result->pointer, u8(), layout.size);
        return result;
    }
    void deallocate(void* pointer, Layout layout) const noexcept {
        if (layout.size == usize()) return;
        auto* block = reinterpret_cast<Block*>(static_cast<byte*>(pointer) - sizeof(Block));
        auto* slab  = block->slab;
        stats_.live_bytes -= layout.size;
        stats_.live_block_bytes -= slab->stride;
        --slab->live;
        if (slab->index == usize::MAX) {
            release(slab);
            return;
        }
        if (! slab->free) make_available(slab);
        block->next = slab->free;
        slab->free  = block;
        if (slab->live == usize()) {
            stats_.retained_bytes += slab->layout.size;
            if (stats_.retained_bytes > options_.retained_bytes) release(slab);
        }
    }
    auto trim(usize retain_bytes = {}) const -> PoolTrimReport {
        PoolTrimReport report;
        auto*          slab = slabs_;
        while (slab && stats_.retained_bytes > retain_bytes) {
            auto* next = slab->next;
            if (slab->live == usize()) {
                report.bytes += slab->layout.size;
                ++report.slabs;
                release(slab);
            }
            slab = next;
        }
        ++stats_.trims;
        return report;
    }
    auto statistics() const noexcept -> PoolStatistics { return stats_; }
    auto upstream_statistics() const
        requires requires { upstream_.statistics(); }
    {
        return upstream_.statistics();
    }
};
} // namespace rstd::alloc
namespace rstd
{
template<typename A>
struct Impl<alloc::Allocator, alloc::PoolAllocator<A>>
    : DefaultInImpl<alloc::Allocator, alloc::PoolAllocator<A>> {
    auto allocate(alloc::Layout l) const { return this->self().allocate(l); }
    auto grow(void* p, alloc::Layout old_layout, alloc::Layout new_layout) const {
        return this->self().grow(p, old_layout, new_layout);
    }
    auto allocate_zeroed(alloc::Layout l) const { return this->self().allocate_zeroed(l); }
    void deallocate(void* p, alloc::Layout l) const noexcept { this->self().deallocate(p, l); }
};
} // namespace rstd

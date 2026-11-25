module;
#include <rstd/macro.hpp>
export module rstd.alloc:pool;
import :alloc;
using namespace rstd::prelude;

export namespace rstd::alloc
{
enum class PoolSizeClasses
{
    PowersOfTwo,
    Fine
};
struct PoolOptions {
    usize slab_bytes { 262144 }, max_pooled_block_bytes { 65536 }, retained_bytes { 1048576 };
    PoolSizeClasses size_classes { PoolSizeClasses::PowersOfTwo };
    // Zero keeps fixed slabs; otherwise slab_bytes caps their size before rounding.
    usize target_blocks_per_slab {}, min_slab_bytes { 16384 }, slab_granularity { 1 };
};
struct PoolSizeClass {
    usize index, capacity;
};
struct PoolStatistics {
    usize live_bytes {}, live_block_bytes {}, peak_live_bytes {}, reserved_bytes {};
    usize retained_bytes {}, metadata_bytes {}, slabs {}, allocations {}, reuses {}, failures {};
    // Resize requests include failures; copied_bytes counts successful moving resize payloads.
    usize grows {}, shrinks {}, in_place_resizes {}, copied_bytes {};
    usize trims {}, released_bytes {};
};
struct PoolTrimReport {
    usize bytes {}, slabs {};
};

template<typename Upstream = ::alloc::Global>
class PoolAllocator {
public:
    static constexpr unsigned MAX_CLASSES = 68;

private:
    static constexpr usize MAX_CAPACITY { 131072 };
    struct Slab;
    struct alignas(16) Block {
        Slab* slab;
        union {
            Block*       next;
            rstd::size_t size;
        };
    };
    static_assert(sizeof(Block) == 16);
    struct Slab {
        Slab * previous {}, *next {}, *available_previous {}, *available_next {};
        Slab * empty_previous {}, *empty_next {};
        Block* free {};
        Layout layout;
        usize  index, stride, live {}, slots;
    };
    Upstream               upstream_;
    PoolOptions            options_;
    mutable PoolStatistics stats_;
    mutable Slab*          slabs_ {};
    mutable Slab *         empty_head_ {}, *empty_tail_ {};
    mutable Slab*          partial_[MAX_CLASSES] {};
    mutable Slab*          empty_[MAX_CLASSES] {};

    auto size_class(usize size) const -> PoolSizeClass {
        usize capacity(16), index;
        if (options_.size_classes == PoolSizeClasses::Fine) {
            usize base, step(16);
            if (size > usize(65536)) {
                base  = usize(65536);
                step  = usize(16384);
                index = usize(64);
            } else if (size > usize(16384)) {
                base  = usize(16384);
                step  = usize(4096);
                index = usize(52);
            } else if (size > usize(4096)) {
                base  = usize(4096);
                step  = usize(1024);
                index = usize(40);
            } else if (size > usize(1024)) {
                base  = usize(1024);
                step  = usize(256);
                index = usize(28);
            } else if (size > usize(256)) {
                base  = usize(256);
                step  = usize(64);
                index = usize(16);
            }
            auto offset = (size - base - usize(1)) / step;
            index += offset;
            capacity = base + (offset + usize(1)) * step;
        } else {
            while (capacity < size) {
                capacity *= usize(2);
                ++index;
            }
        }
        return { index, capacity };
    }
    void make_available(Slab* slab, Slab*& head) const {
        slab->available_next = head;
        if (head) head->available_previous = slab;
        head = slab;
    }
    void remove_available(Slab* slab, Slab*& head) const {
        if (slab->available_previous)
            slab->available_previous->available_next = slab->available_next;
        else
            head = slab->available_next;
        if (slab->available_next)
            slab->available_next->available_previous = slab->available_previous;
        slab->available_next = slab->available_previous = nullptr;
    }
    void make_empty(Slab* slab) const {
        make_available(slab, empty_[slab->index.to_primitive()]);
        slab->empty_previous = empty_tail_;
        if (empty_tail_)
            empty_tail_->empty_next = slab;
        else
            empty_head_ = slab;
        empty_tail_ = slab;
        stats_.retained_bytes += slab->layout.size();
    }
    void remove_empty(Slab* slab) const {
        remove_available(slab, empty_[slab->index.to_primitive()]);
        if (slab->empty_previous)
            slab->empty_previous->empty_next = slab->empty_next;
        else
            empty_head_ = slab->empty_next;
        if (slab->empty_next)
            slab->empty_next->empty_previous = slab->empty_previous;
        else
            empty_tail_ = slab->empty_previous;
        slab->empty_previous = slab->empty_next = nullptr;
        stats_.retained_bytes -= slab->layout.size();
    }
    void release(Slab* slab) const noexcept {
        auto layout = slab->layout;
        if (slab->index != usize::MAX) {
            debug_assert(slab->live == usize());
            remove_empty(slab);
        }
        if (slab->previous)
            slab->previous->next = slab->next;
        else
            slabs_ = slab->next;
        if (slab->next) slab->next->previous = slab->previous;
        stats_.metadata_bytes -= usize(sizeof(Slab)) + slab->slots * usize(sizeof(Block));
        stats_.reserved_bytes -= layout.size();
        stats_.released_bytes += layout.size();
        --stats_.slabs;
        [[unsafe]] {
            rstd::destroy_at(slab);
            as<Allocator>(upstream_).deallocate(slab, layout);
        }
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
        auto stride = payload + usize(sizeof(Block));
        if (pooled) {
            auto budget = size->max(options_.slab_bytes);
            if (options_.target_blocks_per_slab != usize()) {
                auto overhead = *offset - usize(sizeof(Block));
                auto count    = options_.target_blocks_per_slab.min((budget - overhead) / stride);
                *size = (overhead + count * stride).max(options_.min_slab_bytes.min(budget));
            } else
                *size = budget;
        }
        auto rounded = Layout::from_size_align(*size, options_.slab_granularity);
        if (rounded.is_none()) return nullptr;
        *size       = rounded->pad_to_align().size();
        auto layout = Layout::from_size_align(*size, alignment);
        if (layout.is_none()) return nullptr;
        auto memory = as<Allocator>(upstream_).allocate(*layout);
        if (memory.is_err()) return nullptr;
        auto* base  = static_cast<byte*>(memory->pointer);
        auto  slots = pooled ? (*size - *offset + usize(sizeof(Block))) / stride : usize(1);
        auto* slab  = rstd::construct_at(reinterpret_cast<Slab*>(base),
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
                Block { .slab = slab, .next = slab->free });
            slab->free = block;
        }
        stats_.reserved_bytes += *size;
        stats_.metadata_bytes += usize(sizeof(Slab)) + slots * usize(sizeof(Block));
        ++stats_.slabs;
        if (pooled) make_empty(slab);
        return slab;
    }
    auto resize(void* pointer, Layout old_layout, Layout new_layout, bool zeroed) const
        -> Result<Allocation, AllocError> {
        if (old_layout.size() != usize() && new_layout.size() != usize()) {
            [[unsafe]] {
                auto* block = reinterpret_cast<Block*>(static_cast<byte*>(pointer) - sizeof(Block));
                auto  capacity = block->slab->stride - usize(sizeof(Block));
                if (new_layout.size() <= capacity && reinterpret_cast<rstd::uintptr_t>(pointer) %
                                                             new_layout.align().to_primitive() ==
                                                         0) {
                    if (zeroed && new_layout.size() > old_layout.size())
                        rstd::mem::memset(static_cast<byte*>(pointer) +
                                              old_layout.size().to_primitive(),
                                          u8(),
                                          new_layout.size() - old_layout.size());
                    stats_.live_bytes -= old_layout.size();
                    stats_.live_bytes += new_layout.size();
                    stats_.peak_live_bytes = stats_.peak_live_bytes.max(stats_.live_bytes);
                    block->size            = new_layout.size().to_primitive();
                    ++stats_.in_place_resizes;
                    return Ok(Allocation { pointer, new_layout.size() });
                }
            }
        }
        auto result = zeroed ? allocate_zeroed(new_layout) : allocate(new_layout);
        if (result.is_err()) return result;
        auto copied = old_layout.size().min(new_layout.size());
        [[unsafe]] {
            if (copied != usize()) rstd::mem::memcpy(result->pointer, pointer, copied);
            stats_.copied_bytes += copied;
            deallocate(pointer, old_layout);
        }
        return result;
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
    auto class_count() const -> usize {
        auto limit = options_.max_pooled_block_bytes.min(MAX_CAPACITY);
        if (limit < usize(16)) return usize();
        auto bucket = size_class(limit);
        return bucket.index + usize(bucket.capacity == limit ? 1 : 0);
    }
    auto classify(Layout layout) const -> Option<PoolSizeClass> {
        if (layout.size() == usize() || layout.size() > MAX_CAPACITY || layout.align() > usize(16))
            return None();
        auto bucket = size_class(layout.size());
        if (bucket.capacity > options_.max_pooled_block_bytes) return None();
        return Some(bucket);
    }
    auto good_size(Layout layout) const -> usize {
        auto bucket = classify(layout);
        return bucket.is_some() ? bucket->capacity : layout.size();
    }
    // Pointer queries require a live, nonzero allocation from this pool.
    auto allocation_size(const void* pointer) const noexcept -> usize {
        auto* block =
            reinterpret_cast<const Block*>(static_cast<const byte*>(pointer) - sizeof(Block));
        return usize(block->size);
    }
    auto usable_size(const void* pointer) const noexcept -> usize {
        auto* block =
            reinterpret_cast<const Block*>(static_cast<const byte*>(pointer) - sizeof(Block));
        return block->slab->stride - usize(sizeof(Block));
    }
    auto allocate(Layout layout) const -> Result<Allocation, AllocError> {
        if (layout.size() == usize()) return Ok(Allocation { layout.dangling(), usize() });
        auto  size_class = classify(layout);
        auto  index      = size_class.is_some() ? size_class->index : usize::MAX;
        bool  pooled     = size_class.is_some();
        auto* slab       = pooled ? partial_[index.to_primitive()] : nullptr;
        if (pooled && ! slab) slab = empty_[index.to_primitive()];
        bool reused = slab != nullptr;
        if (! slab)
            slab = new_slab(index,
                            pooled ? size_class->capacity : layout.size(),
                            layout.align().max(usize(16)));
        if (! slab) {
            ++stats_.failures;
            return Err(AllocError {});
        }
        bool was_empty = slab->live == usize();
        if (pooled && was_empty) remove_empty(slab);
        auto* block = slab->free;
        slab->free  = block->next;
        block->size = layout.size().to_primitive();
        ++slab->live;
        if (pooled) {
            if (was_empty && slab->free)
                make_available(slab, partial_[index.to_primitive()]);
            else if (! was_empty && ! slab->free)
                remove_available(slab, partial_[index.to_primitive()]);
        }
        stats_.live_bytes += layout.size();
        stats_.live_block_bytes += slab->stride;
        stats_.peak_live_bytes = stats_.peak_live_bytes.max(stats_.live_bytes);
        ++stats_.allocations;
        if (reused) ++stats_.reuses;
        return Ok(Allocation { reinterpret_cast<byte*>(block) + sizeof(Block), layout.size() });
    }
    /// The allocation and layouts must satisfy the Allocator growth contract.
    [[unsafe]]
    auto grow(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size() >= old_layout.size());
        ++stats_.grows;
        return resize(pointer, old_layout, new_layout, false);
    }
    /// The allocation and layouts must satisfy the Allocator growth contract.
    [[unsafe]]
    auto grow_zeroed(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size() >= old_layout.size());
        ++stats_.grows;
        return resize(pointer, old_layout, new_layout, true);
    }
    /// The allocation and layouts must satisfy the Allocator shrink contract.
    [[unsafe]]
    auto shrink(void* pointer, Layout old_layout, Layout new_layout) const
        -> Result<Allocation, AllocError> {
        debug_assert(new_layout.size() <= old_layout.size());
        ++stats_.shrinks;
        return resize(pointer, old_layout, new_layout, false);
    }
    auto allocate_zeroed(Layout layout) const -> Result<Allocation, AllocError> {
        auto result = allocate(layout);
        if (result.is_ok() && layout.size() != usize())
            rstd::mem::memset(result->pointer, u8(), layout.size());
        return result;
    }
    /// The pointer must be a live allocation from this pool with the matching layout.
    [[unsafe]]
    void deallocate(void* pointer, Layout layout) const noexcept {
        if (layout.size() == usize()) return;
        [[unsafe]] {
            auto* block = reinterpret_cast<Block*>(static_cast<byte*>(pointer) - sizeof(Block));
            auto* slab  = block->slab;
            stats_.live_bytes -= layout.size();
            stats_.live_block_bytes -= slab->stride;
            --slab->live;
            if (slab->index == usize::MAX) {
                release(slab);
                return;
            }
            bool was_full = slab->free == nullptr;
            block->next   = slab->free;
            slab->free    = block;
            auto index    = slab->index.to_primitive();
            if (slab->live == usize()) {
                if (! was_full) remove_available(slab, partial_[index]);
                make_empty(slab);
                if (slab->layout.size() > options_.retained_bytes)
                    release(slab);
                else
                    while (stats_.retained_bytes > options_.retained_bytes) release(empty_head_);
            } else if (was_full)
                make_available(slab, partial_[index]);
        }
    }
    auto trim(usize retain_bytes = {}) const -> PoolTrimReport {
        PoolTrimReport report;
        while (empty_head_ && stats_.retained_bytes > retain_bytes) {
            report.bytes += empty_head_->layout.size();
            ++report.slabs;
            release(empty_head_);
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
    [[unsafe]]
    auto grow(void* p, alloc::Layout old_layout, alloc::Layout new_layout) const {
        [[unsafe]] { return this->self().grow(p, old_layout, new_layout); }
    }
    [[unsafe]]
    auto grow_zeroed(void* p, alloc::Layout old_layout, alloc::Layout new_layout) const {
        [[unsafe]] { return this->self().grow_zeroed(p, old_layout, new_layout); }
    }
    [[unsafe]]
    auto shrink(void* p, alloc::Layout old_layout, alloc::Layout new_layout) const {
        [[unsafe]] { return this->self().shrink(p, old_layout, new_layout); }
    }
    auto allocate_zeroed(alloc::Layout l) const { return this->self().allocate_zeroed(l); }
    [[unsafe]]
    void deallocate(void* p, alloc::Layout l) const noexcept {
        [[unsafe]] { this->self().deallocate(p, l); }
    }
};
} // namespace rstd

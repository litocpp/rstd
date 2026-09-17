export module rstd.alloc:range;
export import :vec;

using namespace rstd::prelude;

export namespace alloc
{

using RangeSize  = rstd::uint64_t;
using RangeIndex = rstd::uint32_t;

enum class RangeError
{
    InvalidLayout,
    Exhausted,
    MetadataAllocation,
    InvalidAllocation,
    OutOfOrder,
    IdentityExhausted
};

template<typename Metadata>
class RangeAllocator;
class BumpRangeAllocator;

class RangeId {
    const void* owner_ {};
    RangeSize   generation_ {};
    RangeIndex  slot_ {};
    template<typename>
    friend class RangeAllocator;
    friend class BumpRangeAllocator;
    RangeId(const void* owner, RangeSize generation, RangeIndex slot = 0)
        : owner_(owner), generation_(generation), slot_(slot) {}

public:
    RangeId()                                              = default;
    friend bool operator==(const RangeId&, const RangeId&) = default;
};

struct RangeAllocation {
    RangeId   id;
    RangeSize offset {};
    RangeSize size {};
};

struct RangeStatistics {
    RangeSize capacity {};
    RangeSize requested_bytes {};
    RangeSize occupied_bytes {};
    RangeSize padding_bytes {};
    RangeSize free_bytes {};
    RangeSize largest_free_range {};
    RangeSize allocation_count {};
    RangeSize metadata_bytes {};
    RangeSize search_steps {};
};

inline bool ValidRangeLayout(RangeSize size, RangeSize alignment) noexcept {
    return size != 0 && alignment != 0 && (alignment & (alignment - 1)) == 0;
}

inline auto
AlignedRangeOffset(RangeSize start, RangeSize size, RangeSize alignment, RangeSize end) noexcept
    -> Option<RangeSize> {
    if (! ValidRangeLayout(size, alignment) || start > end) return None();
    const auto mask = alignment - 1;
    if (start > ~RangeSize(0) - mask) return None();
    const auto offset = (start + mask) & ~mask;
    if (offset > end || size > end - offset) return None();
    return Some(RangeSize(offset));
}

/// Ids identify the current reset epoch. Moving or copying does not transfer ids.
class BumpRangeAllocator {
    RangeSize capacity_ {};
    RangeSize cursor_ {};
    RangeSize requested_ {};
    RangeSize count_ {};
    RangeSize generation_ { 1 };

public:
    explicit BumpRangeAllocator(RangeSize capacity = 0): capacity_(capacity) {}
    auto allocate(RangeSize size, RangeSize alignment) -> Result<RangeAllocation, RangeError> {
        if (! ValidRangeLayout(size, alignment)) return Err(RangeError::InvalidLayout);
        if (generation_ == 0) return Err(RangeError::IdentityExhausted);
        auto offset = AlignedRangeOffset(cursor_, size, alignment, capacity_);
        if (offset.is_none()) return Err(RangeError::Exhausted);
        cursor_ = *offset + size;
        requested_ += size;
        ++count_;
        return Ok(RangeAllocation { RangeId(this, generation_), *offset, size });
    }
    bool contains(const RangeAllocation& allocation) const noexcept {
        return allocation.id.owner_ == this && generation_ != 0 &&
               allocation.id.generation_ == generation_ && allocation.size != 0 &&
               allocation.offset <= cursor_ && allocation.size <= cursor_ - allocation.offset;
    }
    bool reset() noexcept {
        cursor_ = requested_ = count_ = 0;
        if (generation_ == ~RangeSize(0))
            generation_ = 0;
        else if (generation_ != 0)
            ++generation_;
        return generation_ != 0;
    }
    auto statistics() const noexcept -> RangeStatistics {
        return { capacity_,
                 requested_,
                 cursor_,
                 cursor_ - requested_,
                 capacity_ - cursor_,
                 capacity_ - cursor_,
                 count_,
                 sizeof(*this),
                 0 };
    }
};

using LinearRangeAllocator = BumpRangeAllocator;

/// Single-block segregated-fit allocator. All access requires external synchronization.
template<typename Metadata = Global>
class RangeAllocator {
    static constexpr RangeIndex none = ~RangeIndex(0);
    struct Node {
        RangeSize  offset {}, size {}, generation {};
        RangeIndex prev { none }, next { none }, free_prev { none }, free_next { none };
        bool       free { true };
    };
    vec::Vec<Node, Metadata> nodes_;
    RangeIndex               lists_[64][8];
    unsigned char            inner_[64] {};
    RangeSize                outer_ {};
    RangeIndex               head_ { none }, spare_ { none };
    RangeSize                capacity_ {}, sequence_ {}, used_ {}, count_ {}, searches_ {};

    static unsigned bucket(RangeSize size) noexcept {
        if (size < 8) return unsigned(size);
        const unsigned exponent = 63U - unsigned(__builtin_clzll(size));
        return (exponent - 2) * 8 + unsigned((size >> (exponent - 3)) - 8);
    }
    Node&       node(RangeIndex index) { return nodes_[usize(index)]; }
    const Node& node(RangeIndex index) const { return nodes_[usize(index)]; }
    void        remove_free(RangeIndex index) noexcept {
        auto&      n = node(index);
        const auto b = bucket(n.size), f = b / 8, s = b % 8;
        if (n.free_prev != none)
            node(n.free_prev).free_next = n.free_next;
        else
            lists_[f][s] = n.free_next;
        if (n.free_next != none) node(n.free_next).free_prev = n.free_prev;
        if (lists_[f][s] == none) {
            inner_[f] &= ~(1U << s);
            if (inner_[f] == 0) outer_ &= ~(RangeSize(1) << f);
        }
        n.free_prev = n.free_next = none;
    }
    void insert_free(RangeIndex index) noexcept {
        auto&      n = node(index);
        const auto b = bucket(n.size), f = b / 8, s = b % 8;
        n.free      = true;
        n.free_prev = none;
        n.free_next = lists_[f][s];
        if (n.free_next != none) node(n.free_next).free_prev = index;
        lists_[f][s] = index;
        inner_[f] |= 1U << s;
        outer_ |= RangeSize(1) << f;
    }
    RangeIndex take_node() {
        if (spare_ != none) {
            auto index  = spare_;
            spare_      = node(index).free_next;
            node(index) = Node {};
            return index;
        }
        auto index = RangeIndex(nodes_.len().to_primitive());
        nodes_.push(Node {});
        return index;
    }
    void recycle_node(RangeIndex index) noexcept {
        node(index)           = Node {};
        node(index).free_next = spare_;
        spare_                = index;
    }
    bool prepare(unsigned needed) {
        auto spare = spare_;
        while (needed && spare != none) {
            --needed;
            spare = node(spare).free_next;
        }
        if (nodes_.len().to_primitive() > none - needed) return false;
        return nodes_.try_reserve(usize(needed)).is_ok();
    }
    void clear_lists() noexcept {
        outer_ = 0;
        for (unsigned f = 0; f < 64; ++f) {
            inner_[f] = 0;
            for (auto& value : lists_[f]) value = none;
        }
    }

public:
    explicit RangeAllocator(RangeSize capacity, Metadata metadata = Metadata {})
        : nodes_(vec::Vec<Node, Metadata>::new_in(rstd::move(metadata))), capacity_(capacity) {
        clear_lists();
    }
    RangeAllocator(const RangeAllocator&)            = delete;
    RangeAllocator& operator=(const RangeAllocator&) = delete;
    RangeAllocator(RangeAllocator&&)                 = delete;
    RangeAllocator& operator=(RangeAllocator&&)      = delete;

    auto allocate(RangeSize size, RangeSize alignment) -> Result<RangeAllocation, RangeError> {
        if (! ValidRangeLayout(size, alignment)) return Err(RangeError::InvalidLayout);
        if (sequence_ == ~RangeSize(0)) return Err(RangeError::IdentityExhausted);
        if (size > capacity_) return Err(RangeError::Exhausted);
        if (head_ == none) {
            if (! prepare(1)) return Err(RangeError::MetadataAllocation);
            head_            = take_node();
            node(head_).size = capacity_;
            insert_free(head_);
        }
        const auto first    = bucket(size);
        auto       groups   = outer_ & (~RangeSize(0) << (first / 8));
        RangeIndex selected = none;
        RangeSize  offset   = 0;
        while (groups && selected == none) {
            auto     f     = unsigned(__builtin_ctzll(groups));
            unsigned slots = inner_[f];
            if (f == first / 8) slots &= 0xffU << (first % 8);
            while (slots && selected == none) {
                auto s = unsigned(__builtin_ctz(slots));
                for (auto i = lists_[f][s]; i != none; i = node(i).free_next) {
                    ++searches_;
                    const auto& n = node(i);
                    auto aligned = AlignedRangeOffset(n.offset, size, alignment, n.offset + n.size);
                    if (aligned.is_some()) {
                        selected = i;
                        offset   = *aligned;
                        break;
                    }
                }
                slots &= slots - 1;
            }
            groups &= groups - 1;
        }
        if (selected == none) return Err(RangeError::Exhausted);
        const auto old     = node(selected);
        const auto padding = offset - old.offset;
        const auto tail    = old.size - padding - size;
        if (! prepare(unsigned(padding != 0) + unsigned(tail != 0)))
            return Err(RangeError::MetadataAllocation);
        remove_free(selected);
        if (padding) {
            auto before         = take_node();
            node(before).offset = old.offset;
            node(before).size   = padding;
            node(before).prev   = old.prev;
            node(before).next   = selected;
            if (old.prev != none)
                node(old.prev).next = before;
            else
                head_ = before;
            node(selected).prev = before;
            insert_free(before);
        }
        if (tail) {
            auto after         = take_node();
            node(after).offset = offset + size;
            node(after).size   = tail;
            node(after).prev   = selected;
            node(after).next   = old.next;
            if (old.next != none) node(old.next).prev = after;
            node(selected).next = after;
            insert_free(after);
        }
        auto& n      = node(selected);
        n.offset     = offset;
        n.size       = size;
        n.free       = false;
        n.generation = ++sequence_;
        used_ += size;
        ++count_;
        return Ok(RangeAllocation { RangeId(this, n.generation, selected), offset, size });
    }
    auto get(RangeId id) const noexcept -> Option<RangeAllocation> {
        if (id.owner_ != this || usize(id.slot_) >= nodes_.len()) return None();
        const auto& n = node(id.slot_);
        if (n.free || n.generation != id.generation_) return None();
        return Some(RangeAllocation { id, n.offset, n.size });
    }
    auto deallocate(RangeId id) noexcept -> Result<empty, RangeError> {
        if (get(id).is_none()) return Err(RangeError::InvalidAllocation);
        auto  index = id.slot_;
        auto& n     = node(index);
        used_ -= n.size;
        --count_;
        if (n.prev != none && node(n.prev).free) {
            const auto previous = n.prev;
            remove_free(previous);
            n.offset = node(previous).offset;
            n.size += node(previous).size;
            n.prev = node(previous).prev;
            if (n.prev != none)
                node(n.prev).next = index;
            else
                head_ = index;
            recycle_node(previous);
        }
        if (n.next != none && node(n.next).free) {
            const auto next = n.next;
            remove_free(next);
            n.size += node(next).size;
            n.next = node(next).next;
            if (n.next != none) node(n.next).prev = index;
            recycle_node(next);
        }
        insert_free(index);
        return Ok(empty {});
    }
    void reset() noexcept {
        nodes_.clear();
        clear_lists();
        head_ = spare_ = none;
        used_ = count_ = 0;
    }
    auto statistics() const noexcept -> RangeStatistics {
        RangeSize largest = head_ == none ? capacity_ : 0;
        for (auto i = head_; i != none; i = node(i).next)
            if (node(i).free && node(i).size > largest) largest = node(i).size;
        return { capacity_,
                 used_,
                 used_,
                 0,
                 capacity_ - used_,
                 largest,
                 count_,
                 sizeof(*this) + RangeSize(nodes_.capacity().to_primitive()) * sizeof(Node),
                 searches_ };
    }
};
} // namespace alloc

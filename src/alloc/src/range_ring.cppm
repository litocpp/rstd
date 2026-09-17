export module rstd.alloc:range_ring;
export import :range;

using namespace rstd::prelude;

export namespace alloc
{
template<typename Metadata>
class RingRangeAllocator;

class RingRangeId {
    const void* owner_ {};
    RangeSize   sequence_ {};
    RangeIndex  slot_ {};
    template<typename>
    friend class RingRangeAllocator;
    RingRangeId(const void* owner, RangeSize sequence, RangeIndex slot)
        : owner_(owner), sequence_(sequence), slot_(slot) {}

public:
    RingRangeId()                                                  = default;
    friend bool operator==(const RingRangeId&, const RingRangeId&) = default;
};

struct RingRangeAllocation {
    RingRangeId id;
    RangeSize   offset {}, size {};
};

/// Owns metadata only. Release is FIFO; the caller establishes completion first.
/// All access requires external synchronization.
template<typename Metadata = Global>
class RingRangeAllocator {
    static constexpr RangeIndex none = ~RangeIndex(0);
    struct Entry {
        RangeSize  offset {}, size {}, begin {}, occupied {}, sequence {};
        RangeIndex next { none };
        bool       live {};
    };
    vec::Vec<Entry, Metadata> entries_;
    RangeSize  capacity_ {}, cursor_ {}, occupied_ {}, requested_ {}, count_ {}, sequence_ {};
    RangeIndex first_ { none }, last_ { none }, spare_ { none };

    Entry&       entry(RangeIndex slot) { return entries_[usize(slot)]; }
    const Entry& entry(RangeIndex slot) const { return entries_[usize(slot)]; }

public:
    explicit RingRangeAllocator(RangeSize capacity, Metadata metadata = Metadata {})
        : entries_(vec::Vec<Entry, Metadata>::new_in(rstd::move(metadata))), capacity_(capacity) {}
    RingRangeAllocator(const RingRangeAllocator&)            = delete;
    RingRangeAllocator& operator=(const RingRangeAllocator&) = delete;
    RingRangeAllocator(RingRangeAllocator&&)                 = delete;
    RingRangeAllocator& operator=(RingRangeAllocator&&)      = delete;

    auto allocate(RangeSize size, RangeSize alignment) -> Result<RingRangeAllocation, RangeError> {
        if (! ValidRangeLayout(size, alignment)) return Err(RangeError::InvalidLayout);
        if (sequence_ == ~RangeSize(0)) return Err(RangeError::IdentityExhausted);
        if (size > capacity_ - occupied_) return Err(RangeError::Exhausted);
        const auto head = first_ == none ? RangeSize(0) : entry(first_).begin;
        auto       offset =
            AlignedRangeOffset(cursor_, size, alignment, cursor_ < head ? head : capacity_);
        RangeSize consumed {};
        if (offset.is_some()) {
            consumed = *offset - cursor_ + size;
        } else if (cursor_ >= head) {
            offset = AlignedRangeOffset(0, size, alignment, head);
            if (offset.is_some()) consumed = (capacity_ - cursor_) + *offset + size;
        }
        if (offset.is_none() || consumed > capacity_ - occupied_) return Err(RangeError::Exhausted);
        RangeIndex slot = spare_;
        if (slot == none) {
            if (entries_.len().to_primitive() >= none || entries_.try_reserve(usize(1)).is_err())
                return Err(RangeError::MetadataAllocation);
            slot = RangeIndex(entries_.len().to_primitive());
            entries_.push(Entry {});
        } else {
            spare_ = entry(slot).next;
        }
        entry(slot) = Entry { *offset, size, cursor_, consumed, ++sequence_, none, true };
        if (last_ == none)
            first_ = slot;
        else
            entry(last_).next = slot;
        last_   = slot;
        cursor_ = *offset + size;
        occupied_ += consumed;
        requested_ += size;
        ++count_;
        return Ok(RingRangeAllocation { RingRangeId(this, sequence_, slot), *offset, size });
    }
    auto get(RingRangeId id) const noexcept -> Option<RingRangeAllocation> {
        if (id.owner_ != this || usize(id.slot_) >= entries_.len()) return None();
        const auto& value = entry(id.slot_);
        if (! value.live || value.sequence != id.sequence_) return None();
        return Some(RingRangeAllocation { id, value.offset, value.size });
    }
    auto front() const noexcept -> Option<RingRangeAllocation> {
        if (first_ == none) return None();
        const auto& value = entry(first_);
        return Some(RingRangeAllocation {
            RingRangeId(this, value.sequence, first_), value.offset, value.size });
    }
    auto release(RingRangeId id) noexcept -> Result<empty, RangeError> {
        if (get(id).is_none()) return Err(RangeError::InvalidAllocation);
        if (id.slot_ != first_) return Err(RangeError::OutOfOrder);
        auto& value = entry(first_);
        first_      = value.next;
        occupied_ -= value.occupied;
        requested_ -= value.size;
        --count_;
        value.live = false;
        value.next = spare_;
        spare_     = id.slot_;
        if (first_ == none) {
            last_   = none;
            cursor_ = 0;
        }
        return Ok(empty {});
    }
    /// Invalidates every id. The caller must finish using the discarded storage first.
    void reset() noexcept {
        entries_.clear();
        first_ = last_ = spare_ = none;
        cursor_ = occupied_ = requested_ = count_ = 0;
    }
    auto statistics() const noexcept -> RangeStatistics {
        RangeSize largest = 0;
        if (first_ == none)
            largest = capacity_;
        else if (occupied_ != capacity_) {
            const auto head = entry(first_).begin;
            if (cursor_ < head)
                largest = head - cursor_;
            else
                largest = capacity_ - cursor_ > head ? capacity_ - cursor_ : head;
        }
        return { capacity_,
                 requested_,
                 occupied_,
                 occupied_ - requested_,
                 capacity_ - occupied_,
                 largest,
                 count_,
                 sizeof(*this) + RangeSize(entries_.capacity().to_primitive()) * sizeof(Entry),
                 0 };
    }
};
} // namespace alloc

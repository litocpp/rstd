export module rstd.alloc:range_stack;
export import :range;

using namespace rstd::prelude;

export namespace alloc
{
template<typename Metadata>
class StackRangeAllocator;

class StackRangeId {
    const void* owner_ {};
    RangeSize   sequence_ {};
    usize       slot_ {};
    template<typename>
    friend class StackRangeAllocator;
    StackRangeId(const void* owner, RangeSize sequence, usize slot)
        : owner_(owner), sequence_(sequence), slot_(slot) {}

public:
    StackRangeId()                                                   = default;
    friend bool operator==(const StackRangeId&, const StackRangeId&) = default;
};

struct StackRangeAllocation {
    StackRangeId id;
    RangeSize    offset {}, size {};
};

/// Owns metadata only. All access requires external synchronization.
template<typename Metadata = Global>
class StackRangeAllocator {
    struct Entry {
        RangeSize offset, size, previous_cursor, sequence;
    };
    vec::Vec<Entry, Metadata> entries_;
    RangeSize                 capacity_ {}, cursor_ {}, requested_ {}, sequence_ {};

public:
    explicit StackRangeAllocator(RangeSize capacity, Metadata metadata = Metadata {})
        : entries_(vec::Vec<Entry, Metadata>::new_in(rstd::move(metadata))), capacity_(capacity) {}
    StackRangeAllocator(const StackRangeAllocator&)            = delete;
    StackRangeAllocator& operator=(const StackRangeAllocator&) = delete;
    StackRangeAllocator(StackRangeAllocator&&)                 = delete;
    StackRangeAllocator& operator=(StackRangeAllocator&&)      = delete;

    auto allocate(RangeSize size, RangeSize alignment) -> Result<StackRangeAllocation, RangeError> {
        if (! ValidRangeLayout(size, alignment)) return Err(RangeError::InvalidLayout);
        if (sequence_ == ~RangeSize(0)) return Err(RangeError::IdentityExhausted);
        auto offset = AlignedRangeOffset(cursor_, size, alignment, capacity_);
        if (offset.is_none()) return Err(RangeError::Exhausted);
        if (entries_.try_reserve(usize(1)).is_err()) return Err(RangeError::MetadataAllocation);
        auto slot = entries_.len();
        entries_.push(Entry { *offset, size, cursor_, ++sequence_ });
        cursor_ = *offset + size;
        requested_ += size;
        return Ok(StackRangeAllocation { StackRangeId(this, sequence_, slot), *offset, size });
    }
    auto get(StackRangeId id) const noexcept -> Option<StackRangeAllocation> {
        if (id.owner_ != this || id.slot_ >= entries_.len()) return None();
        const auto& entry = entries_[id.slot_];
        if (entry.sequence != id.sequence_) return None();
        return Some(StackRangeAllocation { id, entry.offset, entry.size });
    }
    auto top() const noexcept -> Option<StackRangeAllocation> {
        if (entries_.is_empty()) return None();
        auto        slot  = entries_.len() - usize(1);
        const auto& entry = entries_[slot];
        return Some(StackRangeAllocation {
            StackRangeId(this, entry.sequence, slot), entry.offset, entry.size });
    }
    auto pop(StackRangeId id) noexcept -> Result<empty, RangeError> {
        if (get(id).is_none()) return Err(RangeError::InvalidAllocation);
        if (id.slot_ != entries_.len() - usize(1)) return Err(RangeError::OutOfOrder);
        auto entry = entries_.pop().unwrap_unchecked();
        cursor_    = entry.previous_cursor;
        requested_ -= entry.size;
        return Ok(empty {});
    }
    /// Invalidates every id. The caller must finish using the discarded storage first.
    void reset() noexcept {
        entries_.clear();
        cursor_ = requested_ = 0;
    }
    auto statistics() const noexcept -> RangeStatistics {
        return { capacity_,
                 requested_,
                 cursor_,
                 cursor_ - requested_,
                 capacity_ - cursor_,
                 capacity_ - cursor_,
                 RangeSize(entries_.len().to_primitive()),
                 sizeof(*this) + RangeSize(entries_.capacity().to_primitive()) * sizeof(Entry),
                 0 };
    }
};
} // namespace alloc

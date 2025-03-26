module;

#include <atomic>
#include <memory>
#include <optional>

export module mpmc:array;
export import :cache_padded;

namespace mpmc::array
{

template<typename T>
struct Slot {
    /// The current stamp.
    std::atomic_size_t stamp;

    /// The message in this slot. Either read out in `read` or dropped through
    /// `discard_all_messages`.
    std::optional<T> msg;
};

export template<typename T>
struct Channel {
    /// The head of the channel.
    ///
    /// This value is a "stamp" consisting of an index into the buffer, a mark bit, and a lap, but
    /// packed into a single `usize`. The lower bits represent the index, while the upper bits
    /// represent the lap. The mark bit in the head is always zero.
    ///
    /// Messages are popped from the head of the channel.
    utils::CachePadded<std::atomic_size_t> head;

    /// The tail of the channel.
    ///
    /// This value is a "stamp" consisting of an index into the buffer, a mark bit, and a lap, but
    /// packed into a single `usize`. The lower bits represent the index, while the upper bits
    /// represent the lap. The mark bit indicates that the channel is disconnected.
    ///
    /// Messages are pushed into the tail of the channel.
    utils::CachePadded<std::atomic_size_t> tail;

    /// The buffer holding slots.
    std::unique_ptr<Slot<T>> buffer;

    /// The channel capacity.
    std::size_t cap;

    /// A stamp with the value of `{ lap: 1, mark: 0, index: 0 }`.
    std::size_t one_lap;

    /// If this bit is set in the tail, that means the channel is disconnected.
    std::size_t mark_bit;

    /// Senders waiting while the channel is full.
    //    senders: SyncWaker,

    /// Receivers waiting while the channel is empty and not disconnected.
    //    receivers: SyncWaker,
};

} // namespace mpmc::array
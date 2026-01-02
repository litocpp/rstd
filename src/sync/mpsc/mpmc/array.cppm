module;

#include <atomic>
#include <memory>
#include <optional>

export module rstd.sync.mpsc:mpmc.array;
export import :mpmc.utils;
export import rstd.core;
export import rstd.alloc;

using rstd::boxed::Box;
using rstd::sync::mpsc::mpmc::utils::CachePadded;

namespace rstd::sync::mpsc::mpmc::array
{

template<typename T>
struct Slot {
    /// The current stamp.
    Atomic<usize> stamp;

    /// The message in this slot. Either read out in `read` or dropped through
    /// `discard_all_messages`.
    Option<T> msg;
};

struct ArrayToken {
    /// Slot to read from or write to.
    const u8* slot;

    /// Stamp to store into the slot after reading or writing.
    usize stamp;
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
    CachePadded<Atomic<usize>> head;

    /// The tail of the channel.
    ///
    /// This value is a "stamp" consisting of an index into the buffer, a mark bit, and a lap, but
    /// packed into a single `usize`. The lower bits represent the index, while the upper bits
    /// represent the lap. The mark bit indicates that the channel is disconnected.
    ///
    /// Messages are pushed into the tail of the channel.
    CachePadded<Atomic<usize>> tail;

    /// The buffer holding slots.
    Box<Slot<T>[]> buffer;

    /// The channel capacity.
    usize cap;

    /// A stamp with the value of `{ lap: 1, mark: 0, index: 0 }`.
    usize one_lap;

    /// If this bit is set in the tail, that means the channel is disconnected.
    usize mark_bit;

    /// Senders waiting while the channel is full.
    //    senders: SyncWaker,

    /// Receivers waiting while the channel is empty and not disconnected.
    //    receivers: SyncWaker,
};

} // namespace rstd::sync::mpsc::mpmc::array
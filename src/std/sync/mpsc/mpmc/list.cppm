export module rstd:sync.mpsc.mpmc.list;
export import :sync.mpsc.mpmc.context;
export import :sync.mpsc.mpmc.select;
export import :sync.mpsc.mpmc.utils;
export import :sync.mpsc.mpmc.waker;
import :forward;
export import rstd.core;

using rstd_alloc::boxed::Box;
using rstd::mem::maybe_uninit::MaybeUninit;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;

namespace rstd::sync::mpsc::mpmc
{

const usize WRITE { 1 };
const usize READ { 2 };

template<typename T>
struct ListSlot {
    MaybeUninit<T> msg;
    Atomic<usize>  state;
};

template<typename T>
struct Block {
    Atomic<Block*> next;
    ListSlot<T>    slots[31];

    Block(): next(nullptr) {
        for (int i = 0; i < 31; ++i) {
            slots[i].state.store(usize(), Ordering::Relaxed);
        }
    }
};

export struct ListToken {
    const void* block;
    usize       offset;

    static ListToken default_token() { return ListToken { nullptr, usize() }; }
};

export template<typename T>
struct ListPosition {
    Atomic<usize>     index;
    Atomic<Block<T>*> block;
};

export template<typename T>
struct ListChannel {
    CachePadded<ListPosition<T>> head;
    CachePadded<ListPosition<T>> tail;
    SyncWaker                    receivers;

    static auto make() -> Box<ListChannel> {
        auto* block = new Block<T>();
        auto* raw   = new ListChannel {
            .head      = {},
            .tail      = {},
            .receivers = {},
        };
        raw->head->index.store(usize(), Ordering::Relaxed);
        raw->head->block.store(block, Ordering::Relaxed);
        raw->tail->index.store(usize(), Ordering::Relaxed);
        raw->tail->block.store(block, Ordering::Relaxed);
        return Box<ListChannel>::from_raw(mut_ptr<ListChannel>::from_raw_parts(raw));
    }

    auto start_send(ListToken& token) -> bool {
        Backoff backoff;
        while (true) {
            usize tail_idx = tail->index.load(Ordering::Acquire);
            if ((tail_idx & usize(1)) != usize()) return true;

            usize offset = (tail_idx >> u64(1)) % usize(32);
            if (offset == usize(31)) {
                // Install next block if needed
                auto* curr_block = tail->block.load(Ordering::Acquire);
                auto* next_block = curr_block->next.load(Ordering::Acquire);
                if (! next_block) {
                    next_block         = new Block<T>();
                    Block<T>* expected = nullptr;
                    if (! curr_block->next.compare_exchange_strong(
                            expected, next_block, Ordering::Release, Ordering::Acquire)) {
                        delete next_block;
                        next_block = expected;
                    }
                }
                tail->block.compare_exchange_strong(
                    curr_block, next_block, Ordering::Release, Ordering::Acquire);
                tail->index.fetch_add(usize(2), Ordering::Release);
                continue;
            }

            if (tail->index.compare_exchange_weak(
                    tail_idx, tail_idx + usize(2), Ordering::SeqCst, Ordering::Relaxed)) {
                token.block  = tail->block.load(Ordering::Acquire);
                token.offset = offset;
                return true;
            }
            backoff.spin_light();
        }
    }

    auto write(ListToken& token, T msg) -> Result<empty, T> {
        if (! token.block) return Err(rstd::move(msg));
        auto* block = static_cast<Block<T>*>(const_cast<void*>(token.block));
        auto& slot  = block->slots[token.offset.to_primitive()];
        slot.msg.write(rstd::move(msg));
        slot.state.fetch_or(WRITE, Ordering::Release);
        receivers.notify();
        return Ok(empty {});
    }

    auto start_recv(ListToken& token) -> bool {
        Backoff backoff;
        while (true) {
            usize head_idx = head->index.load(Ordering::Acquire);
            usize offset   = (head_idx >> u64(1)) % usize(32);

            if (offset == usize(31)) {
                auto* curr_block = head->block.load(Ordering::Acquire);
                auto* next_block = curr_block->next.load(Ordering::Acquire);
                if (! next_block) {
                    // Check if disconnected
                    if ((tail->index.load(Ordering::Acquire) & usize(1)) != usize()) {
                        token.block = nullptr;
                        return true;
                    }
                    return false; // Empty
                }
                head->block.compare_exchange_strong(
                    curr_block, next_block, Ordering::Release, Ordering::Acquire);
                head->index.fetch_add(usize(2), Ordering::Release);
                // TODO: delete old block? Need refcounting for blocks.
                continue;
            }

            auto* block = head->block.load(Ordering::Acquire);
            auto& slot  = block->slots[offset.to_primitive()];
            if ((slot.state.load(Ordering::Acquire) & WRITE) == usize()) {
                // Mask the disconnect bit (bit 0) before comparing to head_idx:
                // disconnect() does `tail->index |= 1` without bumping the
                // counter, so an unmasked compare misses the empty+disconnected
                // case and the receiver spins forever instead of returning Err.
                usize tail_idx = tail->index.load(Ordering::Acquire);
                if ((tail_idx & ~usize(1)) == head_idx) {
                    if ((tail_idx & usize(1)) != usize()) {
                        token.block = nullptr;
                        return true;
                    }
                    return false; // Empty
                }
                backoff.spin_light();
                continue;
            }

            if (head->index.compare_exchange_weak(
                    head_idx, head_idx + usize(2), Ordering::SeqCst, Ordering::Relaxed)) {
                token.block  = block;
                token.offset = offset;
                return true;
            }
            backoff.spin_light();
        }
    }

    auto read(ListToken& token) -> Result<T, empty> {
        if (! token.block) return Err(empty {});
        auto* block = static_cast<Block<T>*>(const_cast<void*>(token.block));
        auto& slot  = block->slots[token.offset.to_primitive()];
        T     msg   = rstd::move(slot.msg.assume_init_mut());
        slot.state.fetch_or(READ, Ordering::Release);
        return Ok(rstd::move(msg));
    }

    void disconnect() {
        tail->index.fetch_or(usize(1), Ordering::SeqCst);
        receivers.disconnect();
    }

    ~ListChannel() {
        auto* curr = head->block.load(Ordering::Relaxed);
        while (curr) {
            auto* next = curr->next.load(Ordering::Relaxed);

            // Drop messages in the block
            // In a real implementation we'd check head/tail indices,
            // but for a simple cleanup we can just try to drop all written but not read messages.
            // For now, let's just delete the block.
            // TODO: properly drop elements of T.

            delete curr;
            curr = next;
        }
    }
};

} // namespace rstd::sync::mpsc::mpmc

export module rstd:sync.mpmc.list;
export import :sync.mpmc.context;
import :sync.mpmc.error;
export import :sync.mpmc.select;
export import :sync.mpmc.utils;
export import :sync.mpmc.waker;
import :forward;
export import rstd.core;

using rstd_alloc::boxed::Box;
using rstd::mem::maybe_uninit::MaybeUninit;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::fence;
using rstd::sync::atomic::Ordering;

namespace rstd::sync::mpmc::detail
{

namespace channel_api = rstd::sync::mpmc;

const usize WRITE { 1 };
const usize READ { 2 };
const usize DESTROY { 4 };
const usize LIST_LAP { 32 };
const usize LIST_BLOCK_CAP { 31 };
const usize LIST_MARK_BIT { 1 };

template<typename T>
struct ListSlot {
    MaybeUninit<T> msg;
    Atomic<usize>  state;

    void wait_write() const {
        Backoff backoff;
        while ((state.load(Ordering::Acquire) & WRITE) == usize()) backoff.spin_heavy();
    }
};

template<typename T>
struct Block {
    Atomic<Block*> next;
    ListSlot<T>    slots[31];

    Block(): next(nullptr) {
        for (rstd::size_t i = 0; i < 31; ++i) {
            slots[i].state.store(usize(), Ordering::Relaxed);
        }
    }

    auto wait_next() const -> Block* {
        Backoff backoff;
        while (true) {
            auto* next_block = next.load(Ordering::Acquire);
            if (next_block) return next_block;
            backoff.spin_heavy();
        }
    }

    static void destroy(Block* block, usize start) {
        for (rstd::size_t i = start.to_primitive(); i < 30; ++i) {
            auto& slot = block->slots[i];
            if ((slot.state.load(Ordering::Acquire) & READ) == usize() &&
                (slot.state.fetch_or(DESTROY, Ordering::AcqRel) & READ) == usize()) {
                return;
            }
        }
        delete block;
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
        auto* raw = new ListChannel {
            .head      = {},
            .tail      = {},
            .receivers = {},
        };
        raw->head->index.store(usize(), Ordering::Relaxed);
        raw->head->block.store(nullptr, Ordering::Relaxed);
        raw->tail->index.store(usize(), Ordering::Relaxed);
        raw->tail->block.store(nullptr, Ordering::Relaxed);
        return Box<ListChannel>::from_raw(mut_ptr<ListChannel>::from_raw_parts(raw));
    }

    auto start_send(ListToken& token) -> bool {
        Backoff backoff;
        usize   tail_idx   = tail->index.load(Ordering::Acquire);
        auto*   block      = tail->block.load(Ordering::Acquire);
        auto*   next_block = static_cast<Block<T>*>(nullptr);

        while (true) {
            if ((tail_idx & LIST_MARK_BIT) != usize()) {
                delete next_block;
                token.block = nullptr;
                return true;
            }

            usize offset = (tail_idx >> u64(1)) % LIST_LAP;
            if (offset == LIST_BLOCK_CAP) {
                backoff.spin_heavy();
                tail_idx = tail->index.load(Ordering::Acquire);
                block    = tail->block.load(Ordering::Acquire);
                continue;
            }

            if (offset + usize(1) == LIST_BLOCK_CAP && ! next_block) {
                next_block = new Block<T>();
            }

            if (! block) {
                auto* new_block = new Block<T>();
                auto* expected  = block;
                if (tail->block.compare_exchange_strong(
                        expected, new_block, Ordering::Release, Ordering::Relaxed)) {
                    head->block.store(new_block, Ordering::Release);
                    block = new_block;
                } else {
                    if (! next_block) {
                        next_block = new_block;
                    } else {
                        delete new_block;
                    }
                    tail_idx = tail->index.load(Ordering::Acquire);
                    block    = tail->block.load(Ordering::Acquire);
                    continue;
                }
            }

            usize new_tail = tail_idx.wrapping_add(usize(2));
            if (tail->index.compare_exchange_weak(
                    tail_idx, new_tail, Ordering::SeqCst, Ordering::Acquire)) {
                if (offset + usize(1) == LIST_BLOCK_CAP) {
                    tail->block.store(next_block, Ordering::Release);
                    tail->index.fetch_add(usize(2), Ordering::Release);
                    block->next.store(next_block, Ordering::Release);
                    next_block = nullptr;
                }

                token.block  = block;
                token.offset = offset;
                delete next_block;
                return true;
            }

            backoff.spin_light();
            tail_idx = tail->index.load(Ordering::Acquire);
            block    = tail->block.load(Ordering::Acquire);
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
        usize   head_idx = head->index.load(Ordering::Acquire);
        auto*   block    = head->block.load(Ordering::Acquire);

        while (true) {
            usize offset = (head_idx >> u64(1)) % LIST_LAP;
            if (offset == LIST_BLOCK_CAP) {
                backoff.spin_heavy();
                head_idx = head->index.load(Ordering::Acquire);
                block    = head->block.load(Ordering::Acquire);
                continue;
            }

            usize new_head = head_idx.wrapping_add(usize(2));
            if ((new_head & LIST_MARK_BIT) == usize()) {
                fence(Ordering::SeqCst);
                usize tail_idx = tail->index.load(Ordering::Relaxed);

                if ((head_idx >> u64(1)) == (tail_idx >> u64(1))) {
                    if ((tail_idx & LIST_MARK_BIT) != usize()) {
                        token.block = nullptr;
                        return true;
                    }
                    return false;
                }

                if (((head_idx >> u64(1)) / LIST_LAP) != ((tail_idx >> u64(1)) / LIST_LAP)) {
                    new_head |= LIST_MARK_BIT;
                }
            }

            if (! block) {
                backoff.spin_heavy();
                head_idx = head->index.load(Ordering::Acquire);
                block    = head->block.load(Ordering::Acquire);
                continue;
            }

            if (head->index.compare_exchange_weak(
                    head_idx, new_head, Ordering::SeqCst, Ordering::Acquire)) {
                if (offset + usize(1) == LIST_BLOCK_CAP) {
                    auto* next       = block->wait_next();
                    usize next_index = (new_head & ~LIST_MARK_BIT).wrapping_add(usize(2));
                    if (next->next.load(Ordering::Relaxed)) next_index |= LIST_MARK_BIT;
                    head->block.store(next, Ordering::Release);
                    head->index.store(next_index, Ordering::Release);
                }

                token.block  = block;
                token.offset = offset;
                return true;
            }

            backoff.spin_light();
            head_idx = head->index.load(Ordering::Acquire);
            block    = head->block.load(Ordering::Acquire);
        }
    }

    auto read(ListToken& token) -> Result<T, empty> {
        if (! token.block) return Err(empty {});

        auto* block = static_cast<Block<T>*>(const_cast<void*>(token.block));
        auto& slot  = block->slots[token.offset.to_primitive()];
        slot.wait_write();
        T msg = rstd::move(slot.msg.assume_init_mut());
        slot.msg.assume_init_drop();

        if (token.offset + usize(1) == LIST_BLOCK_CAP) {
            Block<T>::destroy(block, usize());
        } else if ((slot.state.fetch_or(READ, Ordering::AcqRel) & DESTROY) != usize()) {
            Block<T>::destroy(block, token.offset + usize(1));
        }
        return Ok(rstd::move(msg));
    }

    auto try_send(T msg) -> Result<empty, channel_api::TrySendError<T>> {
        auto result = send(rstd::move(msg), None());
        if (result.is_ok()) return Ok(empty {});
        return Err(channel_api::TrySendError<T>::Disconnected(
            rstd::move(result).unwrap_err_unchecked().into_inner()));
    }

    auto send(T msg, Option<time::Instant>) -> Result<empty, channel_api::SendTimeoutError<T>> {
        ListToken token = ListToken::default_token();
        start_send(token);
        auto result = write(token, rstd::move(msg));
        if (result.is_ok()) return Ok(empty {});
        return Err(channel_api::SendTimeoutError<T>::Disconnected(
            rstd::move(result).unwrap_err_unchecked()));
    }

    auto try_recv() -> Result<T, channel_api::TryRecvError> {
        ListToken token = ListToken::default_token();
        if (start_recv(token)) {
            auto result = read(token);
            if (result.is_ok()) return Ok(rstd::move(result).unwrap_unchecked());
            return Err(channel_api::TryRecvError::Disconnected);
        }
        return Err(channel_api::TryRecvError::Empty);
    }

    auto recv(Option<time::Instant> deadline) -> Result<T, channel_api::RecvTimeoutError> {
        ListToken token = ListToken::default_token();
        while (true) {
            if (start_recv(token)) {
                auto result = read(token);
                if (result.is_ok()) return Ok(rstd::move(result).unwrap_unchecked());
                return Err(channel_api::RecvTimeoutError::Disconnected);
            }

            if (deadline.is_some() && time::Instant::now() >= *deadline) {
                return Err(channel_api::RecvTimeoutError::Timeout);
            }

            Context::with([&](const Context& cx) {
                auto oper = Operation::hook(&token);
                receivers.register_op(oper, cx);
                if (! is_empty() || is_disconnected()) cx.try_select(Selected::Aborted());

                auto selected = cx.wait_until(deadline);
                if (selected.is_aborted() || selected.is_disconnected()) {
                    receivers.unregister(oper);
                }
            });
        }
    }

    auto disconnect_senders() -> bool {
        usize tail_idx = tail->index.fetch_or(LIST_MARK_BIT, Ordering::SeqCst);
        if ((tail_idx & LIST_MARK_BIT) == usize()) {
            receivers.disconnect();
            return true;
        }
        return false;
    }

    auto disconnect_receivers() -> bool {
        usize tail_idx = tail->index.fetch_or(LIST_MARK_BIT, Ordering::SeqCst);
        if ((tail_idx & LIST_MARK_BIT) == usize()) {
            discard_all_messages();
            return true;
        }
        return false;
    }

    void discard_all_messages() {
        Backoff backoff;
        usize   tail_idx = tail->index.load(Ordering::Acquire);
        while (((tail_idx >> u64(1)) % LIST_LAP) == LIST_BLOCK_CAP) {
            backoff.spin_heavy();
            tail_idx = tail->index.load(Ordering::Acquire);
        }

        usize head_idx = head->index.load(Ordering::Acquire);
        auto* block    = head->block.exchange(nullptr, Ordering::AcqRel);
        while ((head_idx >> u64(1)) != (tail_idx >> u64(1)) && ! block) {
            backoff.spin_heavy();
            block = head->block.exchange(nullptr, Ordering::AcqRel);
        }

        while ((head_idx >> u64(1)) != (tail_idx >> u64(1))) {
            usize offset = (head_idx >> u64(1)) % LIST_LAP;
            if (offset < LIST_BLOCK_CAP) {
                auto& slot = block->slots[offset.to_primitive()];
                slot.wait_write();
                slot.msg.assume_init_drop();
            } else {
                auto* next = block->wait_next();
                delete block;
                block = next;
            }
            head_idx = head_idx.wrapping_add(usize(2));
        }

        delete block;
        head_idx &= ~LIST_MARK_BIT;
        head->index.store(head_idx, Ordering::Release);
    }

    auto len() const -> usize {
        while (true) {
            usize tail_idx = tail->index.load(Ordering::SeqCst);
            usize head_idx = head->index.load(Ordering::SeqCst);
            if (tail->index.load(Ordering::SeqCst) != tail_idx) continue;

            tail_idx &= ~LIST_MARK_BIT;
            head_idx &= ~LIST_MARK_BIT;
            if (((tail_idx >> u64(1)) & (LIST_LAP - usize(1))) == LIST_LAP - usize(1)) {
                tail_idx = tail_idx.wrapping_add(usize(2));
            }
            if (((head_idx >> u64(1)) & (LIST_LAP - usize(1))) == LIST_LAP - usize(1)) {
                head_idx = head_idx.wrapping_add(usize(2));
            }

            usize lap    = (head_idx >> u64(1)) / LIST_LAP;
            usize offset = lap.wrapping_mul(LIST_LAP) << u64(1);
            tail_idx     = tail_idx.wrapping_sub(offset);
            head_idx     = head_idx.wrapping_sub(offset);
            tail_idx     = tail_idx >> u64(1);
            head_idx     = head_idx >> u64(1);
            return tail_idx.wrapping_sub(head_idx).wrapping_sub(tail_idx / LIST_LAP);
        }
    }

    auto capacity() const -> Option<usize> { return None(); }

    auto is_disconnected() const -> bool {
        return (tail->index.load(Ordering::SeqCst) & LIST_MARK_BIT) != usize();
    }

    auto is_empty() const -> bool {
        usize head_idx = head->index.load(Ordering::SeqCst);
        usize tail_idx = tail->index.load(Ordering::SeqCst);
        return (head_idx >> u64(1)) == (tail_idx >> u64(1));
    }

    static auto is_full() -> bool { return false; }

    ~ListChannel() {
        usize head_idx = head->index.load(Ordering::Relaxed) & ~LIST_MARK_BIT;
        usize tail_idx = tail->index.load(Ordering::Relaxed) & ~LIST_MARK_BIT;
        auto* block    = head->block.load(Ordering::Relaxed);

        while (head_idx != tail_idx) {
            usize offset = (head_idx >> u64(1)) % LIST_LAP;
            if (offset < LIST_BLOCK_CAP) {
                block->slots[offset.to_primitive()].msg.assume_init_drop();
            } else {
                auto* next = block->next.load(Ordering::Relaxed);
                delete block;
                block = next;
            }
            head_idx = head_idx.wrapping_add(usize(2));
        }
        delete block;
    }
};

} // namespace rstd::sync::mpmc::detail

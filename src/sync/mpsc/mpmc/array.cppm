module;
#include <rstd/macro.hpp>
export module rstd:sync.mpsc.mpmc.array;
export import :sync.mpsc.mpmc.context;
export import :sync.mpsc.mpmc.select;
export import :sync.mpsc.mpmc.utils;
export import :sync.mpsc.mpmc.waker;
export import :alloc;
export import rstd.core;

using rstd::alloc::boxed::Box;
using rstd::mem::maybe_uninit::MaybeUninit;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using rstd::sync::atomic::fence;

namespace rstd::sync::mpsc::mpmc
{

export struct ArrayToken {
    u8 const* slot;
    usize     stamp;

    static ArrayToken default_token() { return ArrayToken { nullptr, 0 }; }
};

export struct Token {
    ArrayToken array;
};

template<typename T>
struct Slot {
    Atomic<usize>    stamp;
    MaybeUninit<T>   msg;
};

export template<typename T>
struct Channel {
    CachePadded<Atomic<usize>> head;
    CachePadded<Atomic<usize>> tail;
    Box<Slot<T>[]>             buffer;
    usize                      cap;
    usize                      one_lap;
    usize                      mark_bit;
    SyncWaker                  senders;
    SyncWaker                  receivers;

    static auto with_capacity(usize cap) -> Box<Channel> {
        assert(cap > 0);

        usize mark_bit = cppstd::bit_ceil(cap + 1);
        usize one_lap  = mark_bit * 2;

        auto* raw_slots = new Slot<T>[cap];
        for (usize i = 0; i < cap; ++i) {
            raw_slots[i].stamp.store(i, Ordering::Relaxed);
        }
        auto buffer = Box<Slot<T>[]>::from_raw(mut_ptr<Slot<T>[]>::from_raw_parts(raw_slots, cap));

        auto* raw_chan = new Channel {
            .head      = {},
            .tail      = {},
            .buffer    = rstd::move(buffer),
            .cap       = cap,
            .one_lap   = one_lap,
            .mark_bit  = mark_bit,
            .senders   = {},
            .receivers = {},
        };
        raw_chan->head->store(0, Ordering::Relaxed);
        raw_chan->tail->store(0, Ordering::Relaxed);
        
        return Box<Channel>::from_raw(mut_ptr<Channel>::from_raw_parts(raw_chan));
    }

    auto start_send(Token& token) -> bool {
        Backoff backoff;
        usize   tail_val = tail->load(Ordering::Relaxed);

        while (true) {
            if ((tail_val & mark_bit) != 0) {
                token.array.slot  = nullptr;
                token.array.stamp = 0;
                return true;
            }

            usize index = tail_val & (mark_bit - 1);
            usize lap   = tail_val & ~(one_lap - 1);

            auto& slot  = buffer.as_ptr()[index];
            usize stamp = slot.stamp.load(Ordering::Acquire);

            if (tail_val == stamp) {
                usize new_tail;
                if (index + 1 < cap) {
                    new_tail = tail_val + 1;
                } else {
                    new_tail = (lap + one_lap) & ~(one_lap - 1); // wrap lap
                }

                if (tail->compare_exchange_weak(
                        tail_val, new_tail, Ordering::SeqCst, Ordering::Relaxed)) {
                    token.array.slot  = reinterpret_cast<u8 const*>(&slot);
                    token.array.stamp = tail_val + 1;
                    return true;
                } else {
                    backoff.spin_light();
                    tail_val = tail->load(Ordering::Relaxed);
                }
            } else if (stamp + one_lap == tail_val + 1) {
                fence(Ordering::SeqCst);
                usize head_val = head->load(Ordering::Relaxed);

                if (head_val + one_lap == tail_val) {
                    return false; // full
                }

                backoff.spin_light();
                tail_val = tail->load(Ordering::Relaxed);
            } else {
                backoff.spin_heavy();
                tail_val = tail->load(Ordering::Relaxed);
            }
        }
    }

    auto write(Token& token, T msg) -> Result<empty, T> {
        if (token.array.slot == nullptr) {
            return Err(rstd::move(msg));
        }

        auto* slot = reinterpret_cast<Slot<T>*>(const_cast<u8*>(token.array.slot));
        slot->msg.write(rstd::move(msg));
        slot->stamp.store(token.array.stamp, Ordering::Release);

        receivers.notify();
        return Ok(empty {});
    }

    auto start_recv(Token& token) -> bool {
        Backoff backoff;
        usize   head_val = head->load(Ordering::Relaxed);

        while (true) {
            usize index = head_val & (mark_bit - 1);
            usize lap   = head_val & ~(one_lap - 1);

            auto& slot  = buffer.as_ptr()[index];
            usize stamp = slot.stamp.load(Ordering::Acquire);

            if (head_val + 1 == stamp) {
                usize new_head;
                if (index + 1 < cap) {
                    new_head = head_val + 1;
                } else {
                    new_head = (lap + one_lap) & ~(one_lap - 1); // wrap
                }

                if (head->compare_exchange_weak(
                        head_val, new_head, Ordering::SeqCst, Ordering::Relaxed)) {
                    token.array.slot  = reinterpret_cast<u8 const*>(&slot);
                    token.array.stamp = head_val + one_lap;
                    return true;
                } else {
                    backoff.spin_light();
                    head_val = head->load(Ordering::Relaxed);
                }
            } else if (stamp == head_val) {
                fence(Ordering::SeqCst);
                usize tail_val = tail->load(Ordering::Relaxed);

                if ((tail_val & ~mark_bit) == head_val) {
                    if ((tail_val & mark_bit) != 0) {
                        token.array.slot  = nullptr;
                        token.array.stamp = 0;
                        return true;
                    } else {
                        return false; // empty
                    }
                }

                backoff.spin_light();
                head_val = head->load(Ordering::Relaxed);
            } else {
                backoff.spin_heavy();
                head_val = head->load(Ordering::Relaxed);
            }
        }
    }

    auto read(Token& token) -> Result<T, empty> {
        if (token.array.slot == nullptr) {
            return Err(empty {}); // Disconnected
        }

        auto* slot = reinterpret_cast<Slot<T>*>(const_cast<u8*>(token.array.slot));
        T     msg  = rstd::move(slot->msg.assume_init_mut());
        slot->stamp.store(token.array.stamp, Ordering::Release);

        senders.notify();
        return Ok(rstd::move(msg));
    }

    bool try_send(T msg) {
        Token token;
        if (start_send(token)) {
            auto res = write(token, rstd::move(msg));
            return res.is_ok();
        }
        return false;
    }

    auto try_recv() -> Result<T, empty> {
        Token token;
        if (start_recv(token)) {
            return read(token);
        }
        return Err(empty{});
    }

    void disconnect() {
        usize tail_val = tail->fetch_or(mark_bit, Ordering::SeqCst);
        if ((tail_val & mark_bit) == 0) {
            senders.disconnect();
            receivers.disconnect();
        }
    }

    ~Channel() {
        if (buffer) {
            usize h = head->load(Ordering::Relaxed);
            usize t = tail->load(Ordering::Relaxed) & ~mark_bit;

            while (h != t) {
                usize index = h & (mark_bit - 1);
                auto& slot = buffer.as_mut_ptr()[index];
                slot.msg.assume_init_drop();
                
                if (index + 1 < cap) {
                    h += 1;
                } else {
                    h = (h + one_lap) & ~(one_lap - 1);
                }
            }
        }
    }

    bool is_disconnected() const { return (tail->load(Ordering::SeqCst) & mark_bit) != 0; }
    bool is_empty() const { return (head->load(Ordering::SeqCst) == (tail->load(Ordering::SeqCst) & ~mark_bit)); }
    bool is_full() const { return (head->load(Ordering::SeqCst) + one_lap == (tail->load(Ordering::SeqCst) & ~mark_bit)); }
    usize len() const {
        usize h = head->load(Ordering::SeqCst);
        usize t = tail->load(Ordering::SeqCst) & ~mark_bit;

        usize h_idx = h & (mark_bit - 1);
        usize t_idx = t & (mark_bit - 1);

        if (h_idx <= t_idx) {
            return t_idx - h_idx;
        } else {
            return cap - h_idx + t_idx;
        }
    }
    usize capacity() const { return cap; }
};

} // namespace rstd::sync::mpsc::mpmc
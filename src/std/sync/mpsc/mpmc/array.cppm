module;
#include <rstd/macro.hpp>
export module rstd:sync.mpmc.array;
export import :sync.mpmc.context;
import :sync.mpmc.error;
export import :sync.mpmc.select;
export import :sync.mpmc.utils;
export import :sync.mpmc.waker;
export import rstd.core;
import :forward;
import rstd.alloc;

using rstd::mem::maybe_uninit::MaybeUninit;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::fence;
using rstd::sync::atomic::Ordering;
using rstd::alloc::Allocator;
using rstd_alloc::boxed::Box;

namespace rstd::sync::mpmc::detail
{

namespace channel_api = rstd::sync::mpmc;

export struct ArrayToken {
    const void* slot;
    usize       stamp;

    static ArrayToken default_token() { return ArrayToken { nullptr, usize() }; }
};

export struct Token {
    ArrayToken array;

    static Token default_token() { return Token { ArrayToken::default_token() }; }
};

template<typename T>
struct Slot {
    Atomic<usize>  stamp;
    MaybeUninit<T> msg;
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
        rstd_assert(cap > usize());

        usize mark_bit = (cap + usize(1)).next_power_of_two();
        usize one_lap  = mark_bit * usize(2);

        auto slots_storage = as<Allocator>(::alloc::GLOBAL)
                                 .allocate(alloc::Layout::array<Slot<T>>(cap).unwrap())
                                 .unwrap()
                                 .pointer;

        auto* raw_slots = static_cast<Slot<T>*>(slots_storage);
        for (rstd::size_t i = 0; i < cap.to_primitive(); ++i) {
            new (raw_slots + i) Slot<T> {};
            raw_slots[i].stamp.store(usize(i), Ordering::Relaxed);
        }
        auto buffer = Box<Slot<T>[]>::from_raw(mut_ptr<Slot<T>[]>::from_raw_parts(raw_slots, cap));

        auto storage = as<Allocator>(::alloc::GLOBAL)
                           .allocate(alloc::Layout::make<Channel>())
                           .unwrap()
                           .pointer;

        auto* raw_chan = new (storage) Channel {
            .head      = {},
            .tail      = {},
            .buffer    = rstd::move(buffer),
            .cap       = cap,
            .one_lap   = one_lap,
            .mark_bit  = mark_bit,
            .senders   = {},
            .receivers = {},
        };
        raw_chan->head->store(usize(), Ordering::Relaxed);
        raw_chan->tail->store(usize(), Ordering::Relaxed);

        return Box<Channel>::from_raw(mut_ptr<Channel>::from_raw_parts(raw_chan));
    }

    auto start_send(Token& token) -> bool {
        Backoff backoff;
        usize   tail_val = tail->load(Ordering::Relaxed);

        while (true) {
            if ((tail_val & mark_bit) != usize()) {
                token.array.slot  = nullptr;
                token.array.stamp = usize();
                return true;
            }

            usize index = tail_val & (mark_bit - usize(1));
            usize lap   = tail_val & ~(one_lap - usize(1));

            auto& slot  = buffer.as_ptr()[index];
            usize stamp = slot.stamp.load(Ordering::Acquire);

            if (tail_val == stamp) {
                usize new_tail;
                if (index + usize(1) < cap) {
                    new_tail = tail_val.wrapping_add(usize(1));
                } else {
                    new_tail = lap.wrapping_add(one_lap);
                }

                if (tail->compare_exchange_weak(
                        tail_val, new_tail, Ordering::SeqCst, Ordering::Relaxed)) {
                    token.array.slot  = &slot;
                    token.array.stamp = tail_val.wrapping_add(usize(1));
                    return true;
                } else {
                    backoff.spin_light();
                    tail_val = tail->load(Ordering::Relaxed);
                }
            } else if (stamp.wrapping_add(one_lap) == tail_val.wrapping_add(usize(1))) {
                fence(Ordering::SeqCst);
                usize head_val = head->load(Ordering::Relaxed);

                if (head_val.wrapping_add(one_lap) == tail_val) {
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

        auto* slot = static_cast<Slot<T>*>(const_cast<void*>(token.array.slot));
        slot->msg.write(rstd::move(msg));
        slot->stamp.store(token.array.stamp, Ordering::Release);

        receivers.notify();
        return Ok(empty {});
    }

    auto start_recv(Token& token) -> bool {
        Backoff backoff;
        usize   head_val = head->load(Ordering::Relaxed);

        while (true) {
            usize index = head_val & (mark_bit - usize(1));
            usize lap   = head_val & ~(one_lap - usize(1));

            auto& slot  = buffer.as_ptr()[index];
            usize stamp = slot.stamp.load(Ordering::Acquire);

            if (head_val.wrapping_add(usize(1)) == stamp) {
                usize new_head;
                if (index + usize(1) < cap) {
                    new_head = head_val.wrapping_add(usize(1));
                } else {
                    new_head = lap.wrapping_add(one_lap);
                }

                if (head->compare_exchange_weak(
                        head_val, new_head, Ordering::SeqCst, Ordering::Relaxed)) {
                    token.array.slot  = &slot;
                    token.array.stamp = head_val.wrapping_add(one_lap);
                    return true;
                } else {
                    backoff.spin_light();
                    head_val = head->load(Ordering::Relaxed);
                }
            } else if (stamp == head_val) {
                fence(Ordering::SeqCst);
                usize tail_val = tail->load(Ordering::Relaxed);

                if ((tail_val & ~mark_bit) == head_val) {
                    if ((tail_val & mark_bit) != usize()) {
                        token.array.slot  = nullptr;
                        token.array.stamp = usize();
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

        auto* slot = static_cast<Slot<T>*>(const_cast<void*>(token.array.slot));
        T     msg  = rstd::move(slot->msg.assume_init_mut());
        slot->msg.assume_init_drop();
        slot->stamp.store(token.array.stamp, Ordering::Release);

        senders.notify();
        return Ok(rstd::move(msg));
    }

    auto try_send(T msg) -> Result<empty, channel_api::TrySendError<T>> {
        Token token = Token::default_token();
        if (start_send(token)) {
            auto result = write(token, rstd::move(msg));
            if (result.is_ok()) return Ok(empty {});
            return Err(channel_api::TrySendError<T>::Disconnected(
                rstd::move(result).unwrap_err_unchecked()));
        }
        return Err(channel_api::TrySendError<T>::Full(rstd::move(msg)));
    }

    auto send(T msg, Option<time::Instant> deadline)
        -> Result<empty, channel_api::SendTimeoutError<T>> {
        Token token = Token::default_token();
        while (true) {
            if (start_send(token)) {
                auto result = write(token, rstd::move(msg));
                if (result.is_ok()) return Ok(empty {});
                return Err(channel_api::SendTimeoutError<T>::Disconnected(
                    rstd::move(result).unwrap_err_unchecked()));
            }

            if (deadline.is_some() && time::Instant::now() >= *deadline) {
                return Err(channel_api::SendTimeoutError<T>::Timeout(rstd::move(msg)));
            }

            Context::with([&](const Context& cx) {
                auto oper = Operation::hook(&token);
                senders.register_op(oper, cx);
                if (! is_full() || is_disconnected()) cx.try_select(Selected::Aborted());

                auto selected = cx.wait_until(deadline);
                if (selected.is_aborted() || selected.is_disconnected()) {
                    senders.unregister(oper);
                }
            });
        }
    }

    auto try_recv() -> Result<T, channel_api::TryRecvError> {
        Token token = Token::default_token();
        if (start_recv(token)) {
            auto result = read(token);
            if (result.is_ok()) return Ok(rstd::move(result).unwrap_unchecked());
            return Err(channel_api::TryRecvError::Disconnected);
        }
        return Err(channel_api::TryRecvError::Empty);
    }

    auto recv(Option<time::Instant> deadline) -> Result<T, channel_api::RecvTimeoutError> {
        Token token = Token::default_token();
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
        usize tail_val = tail->fetch_or(mark_bit, Ordering::SeqCst);
        if ((tail_val & mark_bit) == usize()) {
            receivers.disconnect();
            return true;
        }
        return false;
    }

    auto disconnect_receivers() -> bool {
        usize tail_val     = tail->fetch_or(mark_bit, Ordering::SeqCst);
        bool  disconnected = (tail_val & mark_bit) == usize();
        if (disconnected) senders.disconnect();
        discard_all_messages(tail_val);
        return disconnected;
    }

    void discard_all_messages(usize tail_val) {
        usize   head_val = head->load(Ordering::Relaxed);
        Backoff backoff;
        tail_val &= ~mark_bit;

        while (true) {
            usize index = head_val & (mark_bit - usize(1));
            usize lap   = head_val & ~(one_lap - usize(1));
            auto& slot  = buffer.as_mut_ptr()[index];
            usize stamp = slot.stamp.load(Ordering::Acquire);

            if (head_val.wrapping_add(usize(1)) == stamp) {
                if (index + usize(1) < cap) {
                    head_val = head_val.wrapping_add(usize(1));
                } else {
                    head_val = lap.wrapping_add(one_lap);
                }
                slot.msg.assume_init_drop();
            } else if (tail_val == head_val) {
                return;
            } else {
                backoff.spin_heavy();
            }
        }
    }

    ~Channel() = default;

    bool is_disconnected() const { return (tail->load(Ordering::SeqCst) & mark_bit) != usize(); }
    bool is_empty() const {
        usize head_val = head->load(Ordering::SeqCst);
        usize tail_val = tail->load(Ordering::SeqCst);
        return head_val == (tail_val & ~mark_bit);
    }
    bool is_full() const {
        usize tail_val = tail->load(Ordering::SeqCst);
        usize head_val = head->load(Ordering::SeqCst);
        return head_val.wrapping_add(one_lap) == (tail_val & ~mark_bit);
    }
    usize len() const {
        while (true) {
            usize tail_val = tail->load(Ordering::SeqCst);
            usize head_val = head->load(Ordering::SeqCst);

            if (tail->load(Ordering::SeqCst) == tail_val) {
                usize head_index = head_val & (mark_bit - usize(1));
                usize tail_index = tail_val & (mark_bit - usize(1));

                if (head_index < tail_index) return tail_index - head_index;
                if (head_index > tail_index) return cap - head_index + tail_index;
                if ((tail_val & ~mark_bit) == head_val) return usize();
                return cap;
            }
        }
    }
    auto capacity() const -> Option<usize> { return Some<usize>(cap); }
};

} // namespace rstd::sync::mpmc::detail

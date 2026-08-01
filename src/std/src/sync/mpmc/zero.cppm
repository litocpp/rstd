module;
#include <rstd/macro.hpp>
export module rstd:sync.mpmc.zero;
export import :sync.mpmc.context;
import :sync.mpmc.error;
export import :sync.mpmc.select;
export import :sync.mpmc.utils;
export import :sync.mpmc.waker;
export import :sync.mutex;
export import rstd.core;
import :forward;
import rstd.alloc;

using rstd::mem::maybe_uninit::MaybeUninit;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using rstd::sync::Mutex;
using rstd_alloc::boxed::Box;

namespace rstd::sync::mpmc::detail
{

namespace channel_api = rstd::sync::mpmc;

export struct ZeroToken {
    void* packet;

    static ZeroToken default_token() { return ZeroToken { nullptr }; }
};

template<typename T>
struct ZeroPacket {
    bool           on_stack;
    Atomic<bool>   ready;
    MaybeUninit<T> msg;
    bool           initialized;

    ZeroPacket(): on_stack(true), ready(false), msg(), initialized(false) {}

    explicit ZeroPacket(T value): on_stack(true), ready(false), msg(), initialized(true) {
        msg.write(rstd::move(value));
    }

    ~ZeroPacket() {
        if (initialized) msg.assume_init_drop();
    }

    void put(T value) {
        msg.write(rstd::move(value));
        initialized = true;
        ready.store(true, Ordering::Release);
    }

    auto take() -> T {
        T value = rstd::move(msg.assume_init_mut());
        msg.assume_init_drop();
        initialized = false;
        return value;
    }

    void wait_ready() const {
        Backoff backoff;
        while (! ready.load(Ordering::Acquire)) backoff.spin_heavy();
    }
};

struct ZeroInner {
    Waker senders;
    Waker receivers;
    bool  is_disconnected;

    ZeroInner(): senders(), receivers(), is_disconnected(false) {}
};

export template<typename T>
struct ZeroChannel {
    Mutex<ZeroInner> inner;

    ZeroChannel(): inner(ZeroInner {}) {}

    static auto make() -> Box<ZeroChannel> {
        auto* raw = new ZeroChannel();
        return Box<ZeroChannel>::from_raw(mut_ptr<ZeroChannel>::from_raw_parts(raw));
    }

    auto write(ZeroToken& token, T msg) -> Result<empty, T> {
        if (! token.packet) return Err(rstd::move(msg));

        auto* packet = static_cast<ZeroPacket<T>*>(token.packet);
        packet->put(rstd::move(msg));
        return Ok(empty {});
    }

    auto read(ZeroToken& token) -> Result<T, empty> {
        if (! token.packet) return Err(empty {});

        auto* packet = static_cast<ZeroPacket<T>*>(token.packet);
        if (packet->on_stack) {
            T msg = packet->take();
            packet->ready.store(true, Ordering::Release);
            return Ok(rstd::move(msg));
        }

        packet->wait_ready();
        T msg = packet->take();
        delete packet;
        return Ok(rstd::move(msg));
    }

    auto try_send(T msg) -> Result<empty, channel_api::TrySendError<T>> {
        ZeroToken token = ZeroToken::default_token();
        {
            auto guard    = inner.lock().unwrap_unchecked();
            auto selected = guard->receivers.try_select();
            if (selected.is_some()) {
                token.packet = selected.unwrap_unchecked().packet;
            } else if (guard->is_disconnected) {
                return Err(channel_api::TrySendError<T>::Disconnected(rstd::move(msg)));
            } else {
                return Err(channel_api::TrySendError<T>::Full(rstd::move(msg)));
            }
        }
        auto result = write(token, rstd::move(msg));
        if (result.is_ok()) return Ok(empty {});
        return Err(
            channel_api::TrySendError<T>::Disconnected(rstd::move(result).unwrap_err_unchecked()));
    }

    auto send(T msg, Option<time::Instant> deadline)
        -> Result<empty, channel_api::SendTimeoutError<T>> {
        return Context::with(
            [&](const Context& cx) -> Result<empty, channel_api::SendTimeoutError<T>> {
                ZeroToken     token = ZeroToken::default_token();
                auto          oper  = Operation::hook(&token);
                ZeroPacket<T> packet { rstd::move(msg) };
                {
                    auto guard    = inner.lock().unwrap_unchecked();
                    auto selected = guard->receivers.try_select();
                    if (selected.is_some()) {
                        token.packet = selected.unwrap_unchecked().packet;
                    } else if (guard->is_disconnected) {
                        return Err(channel_api::SendTimeoutError<T>::Disconnected(packet.take()));
                    } else {
                        guard->senders.register_with_packet(oper, &packet, cx);
                        guard->receivers.notify();
                    }
                }

                if (token.packet) {
                    auto result = write(token, packet.take());
                    if (result.is_ok()) return Ok(empty {});
                    return Err(channel_api::SendTimeoutError<T>::Disconnected(
                        rstd::move(result).unwrap_err_unchecked()));
                }

                auto selected = cx.wait_until(deadline);
                if (selected.is_aborted()) {
                    auto guard = inner.lock().unwrap_unchecked();
                    guard->senders.unregister(oper);
                    return Err(channel_api::SendTimeoutError<T>::Timeout(packet.take()));
                }
                if (selected.is_disconnected()) {
                    auto guard = inner.lock().unwrap_unchecked();
                    guard->senders.unregister(oper);
                    return Err(channel_api::SendTimeoutError<T>::Disconnected(packet.take()));
                }
                if (selected.operation().is_some()) {
                    packet.wait_ready();
                    return Ok(empty {});
                }
                rstd::panic { "zero channel send woke without a selected operation" };
            });
    }

    auto try_recv() -> Result<T, channel_api::TryRecvError> {
        ZeroToken token = ZeroToken::default_token();
        {
            auto guard    = inner.lock().unwrap_unchecked();
            auto selected = guard->senders.try_select();
            if (selected.is_some()) {
                token.packet = selected.unwrap_unchecked().packet;
            } else if (guard->is_disconnected) {
                return Err(channel_api::TryRecvError::Disconnected);
            } else {
                return Err(channel_api::TryRecvError::Empty);
            }
        }
        auto result = read(token);
        if (result.is_ok()) return Ok(rstd::move(result).unwrap_unchecked());
        return Err(channel_api::TryRecvError::Disconnected);
    }

    auto recv(Option<time::Instant> deadline) -> Result<T, channel_api::RecvTimeoutError> {
        return Context::with([&](const Context& cx) -> Result<T, channel_api::RecvTimeoutError> {
            ZeroToken     token = ZeroToken::default_token();
            auto          oper  = Operation::hook(&token);
            ZeroPacket<T> packet;
            {
                auto guard    = inner.lock().unwrap_unchecked();
                auto selected = guard->senders.try_select();
                if (selected.is_some()) {
                    token.packet = selected.unwrap_unchecked().packet;
                } else if (guard->is_disconnected) {
                    return Err(channel_api::RecvTimeoutError::Disconnected);
                } else {
                    guard->receivers.register_with_packet(oper, &packet, cx);
                    guard->senders.notify();
                }
            }

            if (token.packet) {
                auto result = read(token);
                if (result.is_ok()) return Ok(rstd::move(result).unwrap_unchecked());
                return Err(channel_api::RecvTimeoutError::Disconnected);
            }

            auto selected = cx.wait_until(deadline);
            if (selected.is_aborted()) {
                auto guard = inner.lock().unwrap_unchecked();
                guard->receivers.unregister(oper);
                return Err(channel_api::RecvTimeoutError::Timeout);
            }
            if (selected.is_disconnected()) {
                auto guard = inner.lock().unwrap_unchecked();
                guard->receivers.unregister(oper);
                return Err(channel_api::RecvTimeoutError::Disconnected);
            }
            if (selected.operation().is_some()) {
                packet.wait_ready();
                return Ok(packet.take());
            }
            rstd::panic { "zero channel receive woke without a selected operation" };
        });
    }

    auto disconnect() -> bool {
        auto guard = inner.lock().unwrap_unchecked();
        if (guard->is_disconnected) return false;

        guard->is_disconnected = true;
        guard->senders.disconnect();
        guard->receivers.disconnect();
        return true;
    }

    auto is_disconnected() const -> bool {
        auto guard = inner.lock().unwrap_unchecked();
        return guard->is_disconnected;
    }

    static auto len() -> usize { return usize(); }
    static auto capacity() -> Option<usize> { return Some(usize()); }
    static auto is_empty() -> bool { return true; }
    static auto is_full() -> bool { return true; }
};

} // namespace rstd::sync::mpmc::detail

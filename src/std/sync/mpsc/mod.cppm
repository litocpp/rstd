module;
#include <rstd/macro.hpp>
export module rstd:sync.mpsc;
export import :sync.mpsc.mpmc;
export import rstd.core;

namespace rstd::sync::mpsc
{

export template<typename T>
class Receiver {
    mpmc::Receiver<Box<mpmc::Channel<T>>> inner_bounded;
    mpmc::Receiver<Box<mpmc::ListChannel<T>>> inner_unbounded;
    bool is_bounded;

public:
    Receiver(mpmc::Receiver<Box<mpmc::Channel<T>>> i) : inner_bounded(rstd::move(i)), is_bounded(true) {}
    Receiver(mpmc::Receiver<Box<mpmc::ListChannel<T>>> i) : inner_unbounded(rstd::move(i)), is_bounded(false) {}

    auto recv() -> Result<T, empty> {
        if (is_bounded) {
            mpmc::Token token;
            if (inner_bounded->start_recv(token)) {
                return inner_bounded->read(token);
            }

            auto cx = mpmc::Context::make();
            while (true) {
                if (inner_bounded->start_recv(token)) {
                    return inner_bounded->read(token);
                }

                inner_bounded->receivers.register_op(mpmc::Operation::hook(this), cx);
                auto sel = cx.wait_until(None());
                inner_bounded->receivers.unregister(mpmc::Operation::hook(this));

                if (sel.is_disconnected()) {
                    if (inner_bounded->start_recv(token)) {
                        return inner_bounded->read(token);
                    }
                    return Err(empty {});
                }
            }
        } else {
            mpmc::ListToken token;
            if (inner_unbounded->start_recv(token)) {
                return inner_unbounded->read(token);
            }

            auto cx = mpmc::Context::make();
            while (true) {
                if (inner_unbounded->start_recv(token)) {
                    return inner_unbounded->read(token);
                }

                inner_unbounded->receivers.register_op(mpmc::Operation::hook(this), cx);
                auto sel = cx.wait_until(None());
                inner_unbounded->receivers.unregister(mpmc::Operation::hook(this));

                if (sel.is_disconnected()) {
                    if (inner_unbounded->start_recv(token)) {
                        return inner_unbounded->read(token);
                    }
                    return Err(empty {});
                }
            }
        }
    }

    auto try_recv() -> Result<T, empty> {
        if (is_bounded) {
            mpmc::Token token;
            if (inner_bounded->start_recv(token)) {
                return inner_bounded->read(token);
            }
            return Err(empty {});
        } else {
            mpmc::ListToken token;
            if (inner_unbounded->start_recv(token)) {
                return inner_unbounded->read(token);
            }
            return Err(empty {});
        }
    }

    ~Receiver() {
        if (is_bounded) {
            inner_bounded.release([](auto* chan_box) { (*chan_box)->disconnect(); });
        } else {
            inner_unbounded.release([](auto* chan_box) { (*chan_box)->disconnect(); });
        }
    }

    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;
    Receiver(Receiver&&) noexcept = default;
    Receiver& operator=(Receiver&&) noexcept = default;
};

export template<typename T>
class Sender {
    mpmc::Sender<Box<mpmc::ListChannel<T>>> inner;

public:
    Sender(mpmc::Sender<Box<mpmc::ListChannel<T>>> i) : inner(rstd::move(i)) {}

    auto send(T msg) -> Result<empty, T> {
        mpmc::ListToken token {};
        if (inner->start_send(token)) {
            return inner->write(token, rstd::move(msg));
        }
        return Err(rstd::move(msg));
    }

    ~Sender() {
        inner.release([](auto* chan_box) { (*chan_box)->disconnect(); });
    }

    Sender(const Sender& other) : inner(other.inner.acquire()) {}
    Sender& operator=(const Sender& other) {
        if (this != &other) {
            inner.release([](auto* chan_box) { (*chan_box)->disconnect(); });
            inner = other.inner.acquire();
        }
        return *this;
    }
    Sender(Sender&&) noexcept = default;
    Sender& operator=(Sender&&) noexcept = default;
};

export template<typename T>
class SyncSender {
    mpmc::Sender<Box<mpmc::Channel<T>>> inner;

public:
    SyncSender(mpmc::Sender<Box<mpmc::Channel<T>>> i) : inner(rstd::move(i)) {}

    auto send(T msg) -> Result<empty, T> {
        mpmc::Token token;
        if (inner->start_send(token)) {
            return inner->write(token, rstd::move(msg));
        }

        auto cx = mpmc::Context::make();
        while (true) {
            if (inner->start_send(token)) {
                return inner->write(token, rstd::move(msg));
            }

            inner->senders.register_op(mpmc::Operation::hook(this), cx);
            auto sel = cx.wait_until(None());
            inner->senders.unregister(mpmc::Operation::hook(this));

            if (sel.is_disconnected()) {
                return Err(rstd::move(msg));
            }
        }
    }

    auto try_send(T msg) -> Result<empty, T> {
        mpmc::Token token;
        if (inner->start_send(token)) {
            return inner->write(token, rstd::move(msg));
        }
        return Err(rstd::move(msg));
    }

    ~SyncSender() {
        inner.release([](auto* chan_box) { (*chan_box)->disconnect(); });
    }

    SyncSender(const SyncSender& other) : inner(other.inner.acquire()) {}
    SyncSender& operator=(const SyncSender& other) {
        if (this != &other) {
            inner.release([](auto* chan_box) { (*chan_box)->disconnect(); });
            inner = other.inner.acquire();
        }
        return *this;
    }
    SyncSender(SyncSender&&) noexcept = default;
    SyncSender& operator=(SyncSender&&) noexcept = default;
};

export template<typename T>
auto channel() -> cppstd::tuple<Sender<T>, Receiver<T>> {
    auto chan = mpmc::ListChannel<T>::make();
    auto [s, r] = mpmc::new_counter(rstd::move(chan));
    return { Sender<T>(rstd::move(s)), Receiver<T>(rstd::move(r)) };
}

export template<typename T>
auto sync_channel(usize bound) -> cppstd::tuple<SyncSender<T>, Receiver<T>> {
    auto chan = mpmc::Channel<T>::with_capacity(bound);
    auto [s, r] = mpmc::new_counter(rstd::move(chan));
    return { SyncSender<T>(rstd::move(s)), Receiver<T>(rstd::move(r)) };
}

} // namespace rstd::sync::mpsc
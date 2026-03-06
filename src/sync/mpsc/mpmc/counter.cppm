module;
#include <rstd/macro.hpp>
export module rstd:sync.mpsc.mpmc.counter;
export import :alloc;
export import rstd.core;

using rstd::alloc::boxed::Box;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;

namespace rstd::sync::mpsc::mpmc
{

template<typename C>
struct Counter {
    /// The number of senders associated with the channel.
    Atomic<usize> senders;

    /// The number of receivers associated with the channel.
    Atomic<usize> receivers;

    /// Set to `true` if the last sender or the last receiver reference deallocates the channel.
    Atomic<bool> destroy;

    /// The internal channel.
    C chan;

    Counter(C c) : senders(1), receivers(1), destroy(false), chan(rstd::move(c)) {}
};

export template<typename C>
class Sender;
export template<typename C>
class Receiver;

/// Wraps a channel into the reference counter.
export template<typename C>
auto new_counter(C chan) -> cppstd::tuple<Sender<C>, Receiver<C>> {
    auto* counter = Box<Counter<C>>::make(rstd::move(chan)).into_raw().as_raw_ptr();
    return { Sender<C> { counter }, Receiver<C> { counter } };
}

/// The sending side.
export template<typename C>
class Sender {
    Counter<C>* counter_ptr;

public:
    explicit Sender(Counter<C>* c) : counter_ptr(c) {}
    Sender() : counter_ptr(nullptr) {}

    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;

    Sender(Sender&& other) noexcept : counter_ptr(other.counter_ptr) {
        other.counter_ptr = nullptr;
    }
    Sender& operator=(Sender&& other) noexcept {
        if (this != &other) {
            // We can't release here because we don't have the disconnect function.
            // But Sender is meant to be used inside a wrapper that handles release.
            counter_ptr = other.counter_ptr;
            other.counter_ptr = nullptr;
        }
        return *this;
    }

    auto counter() const -> Counter<C>* { return counter_ptr; }
    explicit operator bool() const { return counter_ptr != nullptr; }

    /// Acquires another sender reference.
    auto acquire() const -> Sender<C> {
        if (!counter_ptr) rstd::panic("Sender::acquire on null");
        usize count = counter()->senders.fetch_add(1, Ordering::Relaxed);
        if (count > cppstd::numeric_limits<isize>::max()) {
            rstd::panic("mpsc sender count overflow");
        }
        return Sender { counter_ptr };
    }

    /// Releases the sender reference.
    template<typename F>
    void release(F disconnect) {
        if (counter_ptr) {
            if (counter()->senders.fetch_sub(1, Ordering::AcqRel) == 1) {
                disconnect(rstd::addressof(counter()->chan));

                if (counter()->destroy.exchange(true, Ordering::AcqRel)) {
                    Box<Counter<C>>::from_raw(mut_ptr<Counter<C>>::from_raw_parts(counter_ptr));
                }
            }
            counter_ptr = nullptr;
        }
    }

    auto operator*() const -> C& { return counter()->chan; }
    auto operator->() const -> C& { return counter()->chan; }

    bool operator==(const Sender& other) const { return counter_ptr == other.counter_ptr; }
};

/// The receiving side.
export template<typename C>
class Receiver {
    Counter<C>* counter_ptr;

public:
    explicit Receiver(Counter<C>* c) : counter_ptr(c) {}
    Receiver() : counter_ptr(nullptr) {}

    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    Receiver(Receiver&& other) noexcept : counter_ptr(other.counter_ptr) {
        other.counter_ptr = nullptr;
    }
    Receiver& operator=(Receiver&& other) noexcept {
        if (this != &other) {
            counter_ptr = other.counter_ptr;
            other.counter_ptr = nullptr;
        }
        return *this;
    }

    auto counter() const -> Counter<C>* { return counter_ptr; }
    explicit operator bool() const { return counter_ptr != nullptr; }

    /// Acquires another receiver reference.
    auto acquire() const -> Receiver<C> {
        if (!counter_ptr) rstd::panic("Receiver::acquire on null");
        usize count = counter()->receivers.fetch_add(1, Ordering::Relaxed);
        if (count > cppstd::numeric_limits<isize>::max()) {
            rstd::panic("mpsc receiver count overflow");
        }
        return Receiver { counter_ptr };
    }

    /// Releases the receiver reference.
    template<typename F>
    void release(F disconnect) {
        if (counter_ptr) {
            if (counter()->receivers.fetch_sub(1, Ordering::AcqRel) == 1) {
                disconnect(rstd::addressof(counter()->chan));

                if (counter()->destroy.exchange(true, Ordering::AcqRel)) {
                    Box<Counter<C>>::from_raw(mut_ptr<Counter<C>>::from_raw_parts(counter_ptr));
                }
            }
            counter_ptr = nullptr;
        }
    }

    auto operator*() const -> C& { return counter()->chan; }
    auto operator->() const -> C& { return counter()->chan; }

    bool operator==(const Receiver& other) const { return counter_ptr == other.counter_ptr; }
};

} // namespace rstd::sync::mpsc::mpmc
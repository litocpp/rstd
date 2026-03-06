module;
#include <rstd/macro.hpp>
export module rstd:sync.mpsc.mpmc.waker;
export import :sync.mpsc.mpmc.context;
export import :sync.mpsc.mpmc.select;
export import :sync.mutex;
import :forward;
export import rstd.core;

using rstd_alloc::vec::Vec;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using rstd::sync::Mutex;

namespace rstd::sync::mpsc::mpmc
{

/// Represents a thread blocked on a specific channel operation.
export struct Entry {
    /// The operation.
    Operation oper;

    /// Optional packet.
    void* packet;

    /// Context associated with the thread owning this operation.
    Context cx;
};

/// A queue of threads blocked on channel operations.
struct Waker {
    /// A list of select operations.
    Vec<Entry> selectors;

    /// A list of operations waiting to be ready.
    Vec<Entry> observers;

    Waker() : selectors(Vec<Entry>::make()), observers(Vec<Entry>::make()) {}
    Waker(Waker&&) noexcept = default;
    Waker& operator=(Waker&&) noexcept = default;

    /// Registers a select operation.
    void register_op(Operation oper, const Context& cx) {
        register_with_packet(oper, nullptr, cx);
    }

    /// Registers a select operation and a packet.
    void register_with_packet(Operation oper, void* packet, const Context& cx) {
        selectors.push(Entry { .oper = oper, .packet = packet, .cx = cx });
    }

    /// Unregisters a select operation.
    auto unregister(Operation oper) -> Option<Entry> {
        for (usize i = 0; i < selectors.len(); ++i) {
            if (selectors[i].oper == oper) {
                return Some(selectors.remove(i));
            }
        }
        return None();
    }

    /// Attempts to find another thread's entry, select the operation, and wake it up.
    auto try_select() -> Option<Entry> {
        if (selectors.is_empty()) {
            return None();
        }

        auto thread_id = thread::current().id();

        for (usize i = 0; i < selectors.len(); ++i) {
            auto& selector = selectors[i];
            // Does the entry belong to a different thread?
            if (selector.cx.thread_id() != thread_id) {
                // Try selecting this operation.
                if (selector.cx.try_select(Selected::Op(selector.oper)).is_ok()) {
                    // Provide the packet.
                    selector.cx.store_packet(selector.packet);
                    // Wake the thread up.
                    selector.cx.unpark();

                    // Remove the entry from the queue to keep it clean and improve performance.
                    return Some(selectors.remove(i));
                }
            }
        }
        return None();
    }

    /// Notifies all operations waiting to be ready.
    void notify() {
        // Drain observers
        for (usize i = 0; i < observers.len(); ++i) {
            auto& entry = observers[i];
            if (entry.cx.try_select(Selected::Op(entry.oper)).is_ok()) {
                entry.cx.unpark();
            }
        }
        observers.clear();
    }

    /// Notifies all registered operations that the channel is disconnected.
    void disconnect() {
        for (usize i = 0; i < selectors.len(); ++i) {
            auto& entry = selectors[i];
            if (entry.cx.try_select(Selected::Disconnected()).is_ok()) {
                // Wake the thread up.
                // Here we don't remove the entry from the queue. Registered threads must
                // unregister from the waker by themselves. They might also want to recover the
                // packet value and destroy it, if necessary.
                entry.cx.unpark();
            }
        }

        notify();
    }

    ~Waker() {}
};

/// A waker that can be shared among threads without locking.
///
/// This is a simple wrapper around `Waker` that internally uses a mutex for synchronization.
export class SyncWaker {
    /// The inner `Waker`.
    Mutex<Waker> inner;

    /// `true` if the waker is empty.
    Atomic<bool> is_empty_;

public:
    SyncWaker() : inner(Waker {}), is_empty_(true) {}

    /// Registers the current thread with an operation.
    void register_op(Operation oper, const Context& cx) {
        auto guard = inner.lock().unwrap_unchecked();
        guard->register_op(oper, cx);
        is_empty_.store(guard->selectors.is_empty() && guard->observers.is_empty(), Ordering::SeqCst);
    }

    /// Unregisters an operation previously registered by the current thread.
    auto unregister(Operation oper) -> Option<Entry> {
        auto guard = inner.lock().unwrap_unchecked();
        auto entry = guard->unregister(oper);
        is_empty_.store(guard->selectors.is_empty() && guard->observers.is_empty(), Ordering::SeqCst);
        return entry;
    }

    /// Attempts to find one thread (not the current one), select its operation, and wake it up.
    void notify() {
        if (!is_empty_.load(Ordering::SeqCst)) {
            auto guard = inner.lock().unwrap_unchecked();
            if (!is_empty_.load(Ordering::SeqCst)) {
                guard->try_select();
                guard->notify();
                is_empty_.store(guard->selectors.is_empty() && guard->observers.is_empty(), Ordering::SeqCst);
            }
        }
    }

    /// Notifies all threads that the channel is disconnected.
    void disconnect() {
        auto guard = inner.lock().unwrap_unchecked();
        guard->disconnect();
        is_empty_.store(guard->selectors.is_empty() && guard->observers.is_empty(), Ordering::SeqCst);
    }

    ~SyncWaker() {}
};

} // namespace rstd::sync::mpsc::mpmc
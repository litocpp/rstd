module;
#include <rstd/macro.hpp>
#include <chrono>
export module rstd:sync.mpsc.mpmc.context;
export import :sync.mpsc.mpmc.select;
export import rstd.core;
export import rstd.alloc;
export import :thread;

using rstd::alloc::boxed::Box;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using rstd::sync::Arc;

namespace rstd::sync::mpsc::mpmc
{

// Forward declare for ThreadId
using thread::ThreadId;
using thread::Thread;

struct Inner {
    /// Selected operation.
    Atomic<usize> select;

    /// A slot into which another thread may store a pointer to its `Packet`.
    Atomic<void*> packet;

    /// Thread handle.
    Thread thread;

    /// Thread id.
    ThreadId thread_id;

    Inner(ThreadId id, Thread t)
        : select(Selected::Waiting().val)
        , packet(nullptr)
        , thread(rstd::move(t))
        , thread_id(id) {}
};

export class Context {
    Arc<Inner> inner;

    Context(Arc<Inner> inner) : inner(rstd::move(inner)) {}

public:
    USE_TRAIT(Context)
    Context(const Context& other) : inner(other.inner.clone()) {}
    Context(Context&&) noexcept = default;
    Context& operator=(const Context& other) {
        if (this != &other) {
            inner = other.inner.clone();
        }
        return *this;
    }
    Context& operator=(Context&&) noexcept = default;

    static Context make() {
        return Context { Arc<Inner>::make(thread::current().id(), thread::current()) };
    }

    /// Resets `select` and `packet`.
    void reset() const {
        inner->select.store(Selected::Waiting().val, Ordering::Release);
        inner->packet.store(nullptr, Ordering::Release);
    }

    /// Attempts to select an operation.
    auto try_select(Selected select) const -> Result<empty, Selected> {
        auto expected = Selected::Waiting().val;
        auto result = inner->select.compare_exchange_weak(
            expected, select.val, Ordering::AcqRel, Ordering::Acquire);
        if (result) {
            return Ok(empty {});
        } else {
            return Err(Selected::from_usize(expected));
        }
    }

    /// Stores a packet.
    void store_packet(void* packet) const {
        if (packet != nullptr) {
            inner->packet.store(packet, Ordering::Release);
        }
    }

    /// Waits until an operation is selected and returns it.
    auto wait_until(Option<cppstd::chrono::steady_clock::time_point> deadline) const -> Selected {
        while (true) {
            // Check whether an operation has been selected.
            auto sel = Selected::from_usize(inner->select.load(Ordering::Acquire));
            if (sel != Selected::Waiting()) {
                return sel;
            }

            // If there's a deadline, park the current thread until the deadline is reached.
            if (deadline.is_some()) {
                auto end = deadline.unwrap_unchecked();
                auto now = cppstd::chrono::steady_clock::now();

                if (now < end) {
                    inner->thread.park_timeout(cppstd::chrono::duration_cast<cppstd::chrono::duration<double>>(end - now));
                } else {
                    // The deadline has been reached. Try aborting select.
                    auto res = try_select(Selected::Aborted());
                    if (res.is_ok()) {
                        return Selected::Aborted();
                    } else {
                        return res.unwrap_err_unchecked();
                    }
                }
            } else {
                inner->thread.park();
            }
        }
    }

    /// Unparks the thread this context belongs to.
    void unpark() const {
        inner->thread.unpark();
    }

    /// Returns the id of the thread this context belongs to.
    auto thread_id() const -> ThreadId {
        return inner->thread_id;
    }
};

} // namespace rstd::sync::mpsc::mpmc
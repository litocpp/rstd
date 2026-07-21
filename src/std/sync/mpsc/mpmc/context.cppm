module;
#include <rstd/macro.hpp>
export module rstd:sync.mpmc.context;
export import :sync.mpmc.select;
export import rstd.core;
export import :thread;
export import :time;
import :forward;
import rstd.alloc;

using alloc::sync::Arc;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;

namespace rstd::sync::mpmc::detail
{

using thread::Thread;

export inline auto current_thread_id() -> usize {
    thread_local byte marker {};
    return usize(reinterpret_cast<rstd::uintptr_t>(&marker));
}

struct Inner {
    /// Selected operation.
    Atomic<usize> select;

    /// A slot into which another thread may store a pointer to its `Packet`.
    Atomic<void*> packet;

    /// Thread handle.
    Thread thread;

    /// Thread id.
    usize thread_id;

    Inner(usize id, Thread t)
        : select(Selected::Waiting().val), packet(nullptr), thread(rstd::move(t)), thread_id(id) {}
};

export class Context {
    Arc<Inner> inner;

    Context(Arc<Inner> inner): inner(rstd::move(inner)) {}

    static auto cached_context() -> Option<Context>& {
        thread_local Option<Context> cached = Some(Context::make());
        return cached;
    }

public:
    USE_TRAIT(Context)
    Context(const Context& other): inner(other.inner.clone()) {}
    Context(Context&&) noexcept = default;
    Context& operator=(const Context& other) {
        if (this != &other) {
            inner = other.inner.clone();
        }
        return *this;
    }
    Context& operator=(Context&&) noexcept = default;

    static Context make() {
        return Context { Arc<Inner>::make(current_thread_id(), thread::current()) };
    }

    template<typename F>
    static decltype(auto) with(F&& function) {
        auto& cached = cached_context();
        if (cached.is_none()) {
            auto context = make();
            return rstd::forward<F>(function)(context);
        }

        auto context = cached.take().unwrap_unchecked();
        context.reset();
        using R = decltype(rstd::forward<F>(function)(context));
        if constexpr (rstd::mtp::same_as<R, void>) {
            rstd::forward<F>(function)(context);
            cached = Some(rstd::move(context));
        } else {
            R result = rstd::forward<F>(function)(context);
            cached   = Some(rstd::move(context));
            return result;
        }
    }

    /// Resets `select` and `packet`.
    void reset() const {
        inner->select.store(Selected::Waiting().val, Ordering::Release);
        inner->packet.store(nullptr, Ordering::Release);
    }

    /// Attempts to select an operation.
    auto try_select(Selected select) const -> Result<empty, Selected> {
        auto expected = Selected::Waiting().val;
        auto result   = inner->select.compare_exchange_strong(
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
    /// `deadline` is an optional Instant; if present, parks with a timeout.
    auto wait_until(Option<time::Instant> deadline) const -> Selected {
        while (true) {
            // Check whether an operation has been selected.
            auto sel = Selected::from_usize(inner->select.load(Ordering::Acquire));
            if (sel != Selected::Waiting()) {
                return sel;
            }

            if (deadline.is_some()) {
                auto end = deadline.unwrap_unchecked();
                auto now = time::Instant::now();

                if (now < end) {
                    inner->thread.park_timeout(end.duration_since(now));
                } else {
                    // Deadline reached — try aborting select.
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
    void unpark() const { inner->thread.unpark(); }

    /// Returns the id of the thread this context belongs to.
    auto thread_id() const -> usize { return inner->thread_id; }
};

} // namespace rstd::sync::mpmc::detail

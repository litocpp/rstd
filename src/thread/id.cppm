export module rstd.thread:id;
export import rstd.core;

using rstd::num::nonzero::NonZero;
namespace rstd::thread
{

/// A unique identifier for a running thread.
///
/// `ThreadId` is an opaque object that uniquely identifies each thread created
/// during the lifetime of a process. IDs are guaranteed not to be reused.
/// (No promise about relation to OS thread IDs.)
export class ThreadId {
    NonZero<u64> m_v;

    constexpr ThreadId() = delete;

    constexpr ThreadId(NonZero<u64> v) noexcept: m_v(v) {}

public:
    using value_type = u64;

    // Copyable / comparable like Rust's Copy + Eq
    constexpr ThreadId(const ThreadId&) noexcept            = default;
    constexpr ThreadId& operator=(const ThreadId&) noexcept = default;

    friend constexpr bool operator==(const ThreadId& a, const ThreadId& b) noexcept {
        return a.m_v == b.m_v;
    }

    /// Unstable in Rust: `thread_id_value`
    [[nodiscard]] constexpr NonZero<value_type> as_u64() const noexcept { return m_v; }

    // ---- internal APIs (mirror Rust visibility) ----

    // Generate a new unique thread ID.
    // Rust uses cfg_select! to pick a fast atomic path; here we always use an atomic counter.
    static ThreadId make() {
        auto exhausted = []() {
            panic("failed to generate unique thread ID: bitspace exhausted");
        };

        using rstd::sync::atomic::Atomic;
        using rstd::sync::atomic::Ordering;

        static auto COUNTER = Atomic<u64>(0);
        auto        last    = COUNTER.load(Ordering::Relaxed);
        for (;;) {
            // let Some(id) = last.checked_add(1) else {
            //     exhausted();
            // };

            // match COUNTER.compare_exchange_weak(last, id, Ordering::Relaxed, Ordering::Relaxed) {
            //     Ok(_) => return ThreadId(NonZero::new(id).unwrap()),
            //     Err(id) => last = id,
            // }
        }
    }
    //     // NOTE: Rust counter starts at 0 and returns counter+1 (so never zero).
    //     // We detect wrap to 0 and panic; here we terminate similarly.
    //     // If you have your own panic infra, replace `panic_exhausted()`.
    //     static Atomic<value_type> COUNTER { 0 };

    //     value_type prev = COUNTER.fetch_add(1, rstd::memory_order::relaxed);
    //     value_type id   = prev + 1;

    //     if (id == 0) {
    //         panic("thread id is zero");
    //     }

    //     return { NonZero<value_type>::make(id) };
    // }

    // // Rust: #[cfg(any(not(target_thread_local), target_has_atomic = "64"))]
    // // internal helper for TLS init paths; always available here.
    // static Option<ThreadId> from_u64(value_type v) noexcept {
    //     auto nz = NonZero<value_type>::make(v);
    //     // if (! nz) return {};
    //     return Some(ThreadId(nz));
    // }
};

} // namespace rstd::thread
export module rstd:thread.id;
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
    [[nodiscard]] constexpr auto as_u64() const noexcept { return m_v; }

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
            if (auto id_ = as<u64>(last).checked_add(1)) {
                const auto id = id_.unwrap_unchecked();
                if (COUNTER.compare_exchange_weak(last, id, Ordering::Relaxed, Ordering::Relaxed)) {
                    return { NonZero<u64>::make(id).unwrap() };
                } else {
                    last = id;
                }
            } else {
                exhausted();
            }
        }
    }

    static constexpr auto from_u64(value_type v) noexcept -> Option<ThreadId> {
        return NonZero<value_type>::make(v).map([](auto n) {
            return ThreadId { n };
        });
    }
};

} // namespace rstd::thread
export module rstd.thread.thread:id;
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
    [[nodiscard]]
    constexpr auto as_u64() const noexcept {
        return m_v;
    }

    /// Generates a new unique thread ID using an atomic counter.
    static ThreadId make();

    /// Attempts to construct a ThreadId from a raw u64 value.
    /// \param v The raw identifier value; must be non-zero.
    /// \return Some(ThreadId) if v is non-zero, None otherwise.
    static constexpr auto from_u64(value_type v) noexcept -> Option<ThreadId> {
        return NonZero<value_type>::make(v).map([](auto n) {
            return ThreadId { n };
        });
    }
};

} // namespace rstd::thread

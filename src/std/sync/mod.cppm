/// The synchronization primitives module: mutexes, channels, and reference-counted pointers.
export module rstd:sync;
export import :sync.poison;
export import :sync.mutex;
export import :sync.mpsc;
import rstd.alloc;

namespace rstd::sync
{
export {
    /// A thread-safe reference-counting pointer.
    using ::alloc::sync::Arc;
    /// The raw (non-pinned) form of an Arc.
    using ::alloc::sync::ArcRaw;
    /// A weak reference to an Arc-managed allocation.
    using ::alloc::sync::Weak;
}
} // namespace rstd::sync
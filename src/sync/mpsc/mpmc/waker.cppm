export module rstd::sync::mpsc::mpmc.waker;
export import :context;

namespace mpmc
{


/// Represents a thread blocked on a specific channel operation.

struct Entry {
    /// The operation.

    Operation oper;

    /// Optional packet.

    void* packet;

    /// Context associated with the thread owning this operation.

    Context cx;
};

struct Waker {};

struct SyncWaker {
    std::mutex mutex;
    Waker      inner;
    /// `true` if the waker is empty.
    std::atomic_bool is_empty;
};
} // namespace mpmc::utils
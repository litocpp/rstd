export module rstd.core:sync.atomic;
export import :core;

namespace rstd::sync::atomic
{

export template<typename T>
using Atomic = cppstd::atomic<T>;

using cppstd::memory_order;

export inline void fence(memory_order order) noexcept {
    cppstd::atomic_thread_fence(order);
}

export inline void atomic_thread_fence(memory_order order) noexcept {
    cppstd::atomic_thread_fence(order);
}

export struct Ordering {
    static constexpr memory_order Relaxed = memory_order::relaxed;
    static constexpr memory_order Consume = memory_order::consume;
    static constexpr memory_order Acquire = memory_order::acquire;
    static constexpr memory_order Release = memory_order::release;
    static constexpr memory_order AcqRel  = memory_order::acq_rel;
    static constexpr memory_order SeqCst  = memory_order::seq_cst;
};

} // namespace rstd::sync::atomic
export module rstd.core:sync.atomic;
export import :core;

namespace rstd::sync::atomic
{

export enum class memory_order : int {
    relaxed = __ATOMIC_RELAXED,
    consume = __ATOMIC_CONSUME,
    acquire = __ATOMIC_ACQUIRE,
    release = __ATOMIC_RELEASE,
    acq_rel = __ATOMIC_ACQ_REL,
    seq_cst = __ATOMIC_SEQ_CST
};

export struct Ordering {
    static constexpr memory_order Relaxed = memory_order::relaxed;
    static constexpr memory_order Consume = memory_order::consume;
    static constexpr memory_order Acquire = memory_order::acquire;
    static constexpr memory_order Release = memory_order::release;
    static constexpr memory_order AcqRel  = memory_order::acq_rel;
    static constexpr memory_order SeqCst  = memory_order::seq_cst;
};

export inline void fence(memory_order order) noexcept {
    __atomic_thread_fence(static_cast<int>(order));
}

export inline void compiler_fence(memory_order order) noexcept {
    __atomic_signal_fence(static_cast<int>(order));
}

export template<typename T>
class Atomic {
    T val;

public:
    constexpr Atomic() noexcept = default;
    constexpr Atomic(T v) noexcept: val(v) {}
    Atomic(const Atomic&)                     = delete;
    Atomic& operator=(const Atomic&)          = delete;
    Atomic& operator=(const Atomic&) volatile = delete;

    auto load(memory_order order = memory_order::seq_cst) const noexcept -> T {
        T ret;
        __atomic_load(&val, &ret, static_cast<int>(order));
        return ret;
    }

    auto load(memory_order order = memory_order::seq_cst) const volatile noexcept -> T {
        T ret;
        __atomic_load(&val, &ret, static_cast<int>(order));
        return ret;
    }

    void store(T v, memory_order order = memory_order::seq_cst) noexcept {
        __atomic_store(&val, &v, static_cast<int>(order));
    }

    void store(T v, memory_order order = memory_order::seq_cst) volatile noexcept {
        __atomic_store(&val, &v, static_cast<int>(order));
    }

    auto exchange(T v, memory_order order = memory_order::seq_cst) noexcept -> T {
        T ret;
        __atomic_exchange(&val, &v, &ret, static_cast<int>(order));
        return ret;
    }

    auto exchange(T v, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        T ret;
        __atomic_exchange(&val, &v, &ret, static_cast<int>(order));
        return ret;
    }

    auto compare_exchange_weak(T& expected, T desired, memory_order success = memory_order::seq_cst,
                               memory_order failure = memory_order::seq_cst) noexcept -> bool {
        return __atomic_compare_exchange(
            &val, &expected, &desired, true, static_cast<int>(success), static_cast<int>(failure));
    }

    auto compare_exchange_weak(T& expected, T desired, memory_order success = memory_order::seq_cst,
                               memory_order failure = memory_order::seq_cst) volatile noexcept
        -> bool {
        return __atomic_compare_exchange(
            &val, &expected, &desired, true, static_cast<int>(success), static_cast<int>(failure));
    }

    auto compare_exchange_strong(T& expected, T desired,
                                 memory_order success = memory_order::seq_cst,
                                 memory_order failure = memory_order::seq_cst) noexcept -> bool {
        return __atomic_compare_exchange(
            &val, &expected, &desired, false, static_cast<int>(success), static_cast<int>(failure));
    }

    auto compare_exchange_strong(T& expected, T desired,
                                 memory_order success = memory_order::seq_cst,
                                 memory_order failure = memory_order::seq_cst) volatile noexcept
        -> bool {
        return __atomic_compare_exchange(
            &val, &expected, &desired, false, static_cast<int>(success), static_cast<int>(failure));
    }

    auto fetch_add(T arg, memory_order order = memory_order::seq_cst) noexcept -> T {
        return __atomic_fetch_add(&val, arg, static_cast<int>(order));
    }

    auto fetch_add(T arg, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        return __atomic_fetch_add(&val, arg, static_cast<int>(order));
    }

    auto fetch_sub(T arg, memory_order order = memory_order::seq_cst) noexcept -> T {
        return __atomic_fetch_sub(&val, arg, static_cast<int>(order));
    }

    auto fetch_sub(T arg, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        return __atomic_fetch_sub(&val, arg, static_cast<int>(order));
    }

    auto fetch_and(T arg, memory_order order = memory_order::seq_cst) noexcept -> T {
        return __atomic_fetch_and(&val, arg, static_cast<int>(order));
    }

    auto fetch_and(T arg, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        return __atomic_fetch_and(&val, arg, static_cast<int>(order));
    }

    auto fetch_or(T arg, memory_order order = memory_order::seq_cst) noexcept -> T {
        return __atomic_fetch_or(&val, arg, static_cast<int>(order));
    }

    auto fetch_or(T arg, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        return __atomic_fetch_or(&val, arg, static_cast<int>(order));
    }

    auto fetch_xor(T arg, memory_order order = memory_order::seq_cst) noexcept -> T {
        return __atomic_fetch_xor(&val, arg, static_cast<int>(order));
    }

    auto fetch_xor(T arg, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        return __atomic_fetch_xor(&val, arg, static_cast<int>(order));
    }
};

} // namespace rstd::sync::atomic
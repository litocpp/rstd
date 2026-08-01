export module rstd.core:sync.atomic;
import :num.types;
export import :core;

namespace rstd::sync::atomic
{

/// Atomic memory ordering constraints.
export enum class memory_order : int {
    relaxed = __ATOMIC_RELAXED,
    consume = __ATOMIC_CONSUME,
    acquire = __ATOMIC_ACQUIRE,
    release = __ATOMIC_RELEASE,
    acq_rel = __ATOMIC_ACQ_REL,
    seq_cst = __ATOMIC_SEQ_CST
};

/// Named constants for atomic memory orderings.
export struct Ordering {
    static constexpr memory_order Relaxed = memory_order::relaxed;
    static constexpr memory_order Consume = memory_order::consume;
    static constexpr memory_order Acquire = memory_order::acquire;
    static constexpr memory_order Release = memory_order::release;
    static constexpr memory_order AcqRel  = memory_order::acq_rel;
    static constexpr memory_order SeqCst  = memory_order::seq_cst;
};

/// Issues a thread-level memory fence with the given ordering.
/// \param order The memory ordering constraint.
export inline void fence(memory_order order) noexcept {
    __atomic_thread_fence(static_cast<int>(order));
}

/// Issues a compiler-only memory fence (no CPU instruction emitted).
/// \param order The memory ordering constraint.
export inline void compiler_fence(memory_order order) noexcept {
    __atomic_signal_fence(static_cast<int>(order));
}

/// A generic atomic type for lock-free concurrent access.
/// \tparam T The underlying value type.
export template<typename T>
class Atomic {
    template<typename U, bool = num::Numeric<U>>
    struct StorageFor {
        using Type = U;
    };

    template<typename U>
    struct StorageFor<U, true> {
        using Type = typename U::primitive_type;
    };

public:
    using Native = typename StorageFor<T>::Type;

private:
    static constexpr auto storage_alignment = [] {
        constexpr auto size      = sizeof(Native);
        constexpr auto alignment = alignof(Native);
        if constexpr ((size & (size - 1)) == 0 && size <= 16 && size > alignment) {
            return size;
        } else {
            return alignment;
        }
    }();

    alignas(storage_alignment) Native val {};

    static constexpr auto to_native(T value) noexcept -> Native {
        if constexpr (num::Numeric<T>) {
            return value.to_primitive();
        } else {
            return value;
        }
    }

    static constexpr auto from_native(Native value) noexcept -> T {
        if constexpr (num::Numeric<T>) {
            return T(value);
        } else {
            return value;
        }
    }

public:
    constexpr Atomic() noexcept = default;
    constexpr Atomic(T v) noexcept: val(to_native(v)) {}
    Atomic(const Atomic&)                     = delete;
    Atomic& operator=(const Atomic&)          = delete;
    Atomic& operator=(const Atomic&) volatile = delete;

    auto load(memory_order order = memory_order::seq_cst) const noexcept -> T {
        Native ret;
        __atomic_load(&val, &ret, static_cast<int>(order));
        return from_native(ret);
    }

    auto load(memory_order order = memory_order::seq_cst) const volatile noexcept -> T {
        Native ret;
        __atomic_load(&val, &ret, static_cast<int>(order));
        return from_native(ret);
    }

    void store(T v, memory_order order = memory_order::seq_cst) noexcept {
        auto native = to_native(v);
        __atomic_store(&val, &native, static_cast<int>(order));
    }

    void store(T v, memory_order order = memory_order::seq_cst) volatile noexcept {
        auto native = to_native(v);
        __atomic_store(&val, &native, static_cast<int>(order));
    }

    auto exchange(T v, memory_order order = memory_order::seq_cst) noexcept -> T {
        auto   native = to_native(v);
        Native ret;
        __atomic_exchange(&val, &native, &ret, static_cast<int>(order));
        return from_native(ret);
    }

    auto exchange(T v, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        auto   native = to_native(v);
        Native ret;
        __atomic_exchange(&val, &native, &ret, static_cast<int>(order));
        return from_native(ret);
    }

    auto compare_exchange_weak(T&           expected,
                               T            desired,
                               memory_order success = memory_order::seq_cst,
                               memory_order failure = memory_order::seq_cst) noexcept -> bool {
        auto       native_expected = to_native(expected);
        auto       native_desired  = to_native(desired);
        const bool exchanged       = __atomic_compare_exchange(&val,
                                                               &native_expected,
                                                               &native_desired,
                                                               true,
                                                               static_cast<int>(success),
                                                               static_cast<int>(failure));
        expected                   = from_native(native_expected);
        return exchanged;
    }

    auto compare_exchange_weak(T&           expected,
                               T            desired,
                               memory_order success = memory_order::seq_cst,
                               memory_order failure = memory_order::seq_cst) volatile noexcept
        -> bool {
        auto       native_expected = to_native(expected);
        auto       native_desired  = to_native(desired);
        const bool exchanged       = __atomic_compare_exchange(&val,
                                                               &native_expected,
                                                               &native_desired,
                                                               true,
                                                               static_cast<int>(success),
                                                               static_cast<int>(failure));
        expected                   = from_native(native_expected);
        return exchanged;
    }

    auto compare_exchange_strong(T&           expected,
                                 T            desired,
                                 memory_order success = memory_order::seq_cst,
                                 memory_order failure = memory_order::seq_cst) noexcept -> bool {
        auto       native_expected = to_native(expected);
        auto       native_desired  = to_native(desired);
        const bool exchanged       = __atomic_compare_exchange(&val,
                                                               &native_expected,
                                                               &native_desired,
                                                               false,
                                                               static_cast<int>(success),
                                                               static_cast<int>(failure));
        expected                   = from_native(native_expected);
        return exchanged;
    }

    auto compare_exchange_strong(T&           expected,
                                 T            desired,
                                 memory_order success = memory_order::seq_cst,
                                 memory_order failure = memory_order::seq_cst) volatile noexcept
        -> bool {
        auto       native_expected = to_native(expected);
        auto       native_desired  = to_native(desired);
        const bool exchanged       = __atomic_compare_exchange(&val,
                                                               &native_expected,
                                                               &native_desired,
                                                               false,
                                                               static_cast<int>(success),
                                                               static_cast<int>(failure));
        expected                   = from_native(native_expected);
        return exchanged;
    }

    auto fetch_add(T arg, memory_order order = memory_order::seq_cst) noexcept -> T {
        return from_native(__atomic_fetch_add(&val, to_native(arg), static_cast<int>(order)));
    }

    auto fetch_add(T arg, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        return from_native(__atomic_fetch_add(&val, to_native(arg), static_cast<int>(order)));
    }

    auto fetch_sub(T arg, memory_order order = memory_order::seq_cst) noexcept -> T {
        return from_native(__atomic_fetch_sub(&val, to_native(arg), static_cast<int>(order)));
    }

    auto fetch_sub(T arg, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        return from_native(__atomic_fetch_sub(&val, to_native(arg), static_cast<int>(order)));
    }

    auto fetch_and(T arg, memory_order order = memory_order::seq_cst) noexcept -> T {
        return from_native(__atomic_fetch_and(&val, to_native(arg), static_cast<int>(order)));
    }

    auto fetch_and(T arg, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        return from_native(__atomic_fetch_and(&val, to_native(arg), static_cast<int>(order)));
    }

    auto fetch_or(T arg, memory_order order = memory_order::seq_cst) noexcept -> T {
        return from_native(__atomic_fetch_or(&val, to_native(arg), static_cast<int>(order)));
    }

    auto fetch_or(T arg, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        return from_native(__atomic_fetch_or(&val, to_native(arg), static_cast<int>(order)));
    }

    auto fetch_xor(T arg, memory_order order = memory_order::seq_cst) noexcept -> T {
        return from_native(__atomic_fetch_xor(&val, to_native(arg), static_cast<int>(order)));
    }

    auto fetch_xor(T arg, memory_order order = memory_order::seq_cst) volatile noexcept -> T {
        return from_native(__atomic_fetch_xor(&val, to_native(arg), static_cast<int>(order)));
    }

    auto as_native_ptr() & noexcept [[clang::lifetimebound]] -> Native* { return &val; }
    auto as_native_ptr() const& noexcept [[clang::lifetimebound]] -> const Native* { return &val; }
    auto as_native_ptr() volatile& noexcept [[clang::lifetimebound]] -> volatile Native* {
        return &val;
    }
    auto as_native_ptr() const volatile& noexcept [[clang::lifetimebound]] -> const
        volatile Native* {
        return &val;
    }
    auto as_native_ptr() && -> Native* = delete;
};

} // namespace rstd::sync::atomic

module;
#include <rstd/macro.hpp>
export module rstd:sync.mpsc.mpmc.utils;
export import rstd.core;
export import :thread;

namespace rstd::sync::mpsc::mpmc
{

export template<typename T>
struct alignas(64) CachePadded {
    T value;

    CachePadded() = default;
    CachePadded(T v) : value(rstd::move(v)) {}

    auto operator*() -> T& { return value; }
    auto operator*() const -> const T& { return value; }
    auto operator->() -> T* { return rstd::addressof(value); }
    auto operator->() const -> const T* { return rstd::addressof(value); }
};

export class Backoff {
    usize step;
    static constexpr u32 SPIN_LIMIT = 6;

public:
    Backoff() : step(0) {}

    void spin_light() {
        auto limit = step < SPIN_LIMIT ? step : SPIN_LIMIT;
        usize count = 1 << limit;
        for (usize i = 0; i < count; ++i) {
            hint::spin_loop();
        }
        step += 1;
    }

    void spin_heavy() {
        if (step <= SPIN_LIMIT) {
            usize count = 1 << step;
            for (usize i = 0; i < count; ++i) {
                hint::spin_loop();
            }
        } else {
            thread::yield_now();
        }
        step += 1;
    }
};

} // namespace rstd::sync::mpsc::mpmc
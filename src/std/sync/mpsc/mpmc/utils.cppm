export module rstd:sync.mpsc.mpmc.utils;
export import rstd.core;
export import :thread;

namespace rstd::sync::mpsc::mpmc
{

export template<typename T>
struct alignas(64) CachePadded {
    T value;

    CachePadded() = default;
    CachePadded(T v): value(rstd::move(v)) {}

    auto operator*() -> T& { return value; }
    auto operator*() const -> const T& { return value; }
    auto operator->() -> T* { return rstd::addressof(value); }
    auto operator->() const -> const T* { return rstd::addressof(value); }
};

export class Backoff {
    rstd::uint32_t                  step {};
    static constexpr rstd::uint32_t SPIN_LIMIT = 6;

public:
    Backoff() = default;

    void spin_light() {
        auto         limit = step < SPIN_LIMIT ? step : SPIN_LIMIT;
        rstd::size_t count = rstd::size_t(1) << limit;
        for (rstd::size_t i = 0; i < count; ++i) {
            hint::spin_loop();
        }
        ++step;
    }

    void spin_heavy() {
        if (step <= SPIN_LIMIT) {
            rstd::size_t count = rstd::size_t(1) << step;
            for (rstd::size_t i = 0; i < count; ++i) {
                hint::spin_loop();
            }
        } else {
            thread::yield_now();
        }
        ++step;
    }
};

} // namespace rstd::sync::mpsc::mpmc

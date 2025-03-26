module;
#include <atomic>
#include <chrono>
#include <limits>

export module rstd.sys:sync.thread_parking.futex;
import :pal;

namespace parking::futex
{
using Futex = pal::SmallFutex;
using State = pal::SmallPrimitive;

export class Parker {
private:
    static constexpr State PARKED   = std::numeric_limits<State>::max();
    static constexpr State EMPTY    = 0;
    static constexpr State NOTIFIED = 1;

    Futex state;

public:
    Parker();
    void park();
    void park_timeout(std::chrono::duration<double> timeout);
    void unpark();
};
} // namespace futex
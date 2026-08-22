export module rstd:sys.sync.thread_parking.futex;
import :sys.pal;
export import rstd.core;

namespace rstd::sys::sync::thread_parking::futex
{
using Futex = pal::futex::SmallFutex;
using State = pal::futex::SmallPrimitive;

export class Parker {
private:
    static constexpr State PARKED = State::MAX;
    static constexpr State EMPTY {};
    static constexpr State NOTIFIED { static_cast<State::primitive_type>(1) };

    Futex state;

public:
    Parker();
    ~Parker();
    void park();
    void park_timeout(rstd::time::Duration timeout);
    void unpark();
};
} // namespace rstd::sys::sync::thread_parking::futex

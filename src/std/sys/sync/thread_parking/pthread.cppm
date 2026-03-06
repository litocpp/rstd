export module rstd:sys.sync.thread_parking.pthread;
export import :sys.pal;
export import rstd.core;

using rstd::sync::atomic::Atomic;
using rstd::sys::pal::Mutex;
using rstd::sys::pal::Condvar;

namespace rstd::sys::sync::thread_parking::pthread
{
export class Parker {
private:
    static constexpr usize EMPTY    = 0;
    static constexpr usize PARKED   = 1;
    static constexpr usize NOTIFIED = 2;

    Atomic<usize> state;
    Mutex         lock;
    Condvar       cvar;

public:
    Parker();
    void park();
    void park_timeout(const cppstd::chrono::duration<double>& dur);
    void unpark();
};
} // namespace rstd::sys::sync::thread_parking::pthread
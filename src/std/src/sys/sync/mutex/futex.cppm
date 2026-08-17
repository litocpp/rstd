export module rstd:sys.sync.mutex.futex;
export import :sys.pal;

namespace rstd::sys::sync::mutex::futex
{

using Futex = pal::futex::SmallFutex;
using State = pal::futex::SmallPrimitive;

export class Mutex {
    Futex m_futex;

    Mutex() noexcept;

public:
    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;

    static auto make() noexcept -> Mutex;
    [[nodiscard]]
    bool try_lock() noexcept;
    void lock() noexcept;
    void unlock() noexcept;

private:
    [[gnu::cold]]
    void lock_contended() noexcept;
    auto spin() noexcept -> State;
    void wake() noexcept;
};

} // namespace rstd::sys::sync::mutex::futex

module;
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <chrono>

export module rstd.sys:sync.thread_parking.pthread;

namespace rstd::sync::thread_parking::pthread
{
export class Parker {
private:
    static constexpr std::size_t EMPTY    = 0;
    static constexpr std::size_t PARKED   = 1;
    static constexpr std::size_t NOTIFIED = 2;

    std::atomic_size_t      state;
    std::mutex              lock;
    std::condition_variable cvar;

public:
    Parker();
    void park();
    void park_timeout(const std::chrono::duration<double>& dur);
    void unpark();
};
} // namespace parking. pthread
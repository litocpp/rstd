module;
#include <rstd/macro.hpp>
#include <gtest/gtest.h>
#include <thread>

module rstd;
#if RSTD_OS_UNIX && ! RSTD_OS_WINDOWS
import :sys.sync.condvar.pthread;

namespace rstd_condvar_pthread_test
{
using rstd::sys::sync::condvar::pthread::Condvar;
using rstd::sys::sync::mutex::pthread::Mutex;

TEST(CondvarPthread, WaitWakesAfterNotifyOne) {
    auto mutex = Mutex::make();
    auto cvar  = Condvar::make();
    bool ready = false;

    std::thread waiter([&] {
        mutex.lock();
        while (! ready) {
            cvar.wait(mutex);
        }
        mutex.unlock();
    });

    mutex.lock();
    ready = true;
    mutex.unlock();
    cvar.notify_one();

    waiter.join();
}

TEST(CondvarPthread, WaitTimeoutReportsTimeout) {
    auto mutex = Mutex::make();
    auto cvar  = Condvar::make();

    mutex.lock();
    auto notified = cvar.wait_timeout(mutex, rstd::time::Duration::from_millis(5));
    mutex.unlock();

    EXPECT_FALSE(notified);
}

} // namespace rstd_condvar_pthread_test
#endif

#if defined(__unix__) && ! defined(_WIN32)

#include <gtest/gtest.h>
import rstd;
using rstd::sys::sync::mutex::pthread::Mutex;

#    define RSTD_TEST_GROUP MutexPthread
#    include "common.hpp"

TEST(MutexPthread, DestructorDoesNotHangIfLockedElsewhere) {
    // Purpose of this test:
    // - When Mutex is destroyed while the underlying pthread_mutex_t is locked,
    //   your implementation should fail try_lock and then leak the raw object via into_raw()
    //   to mirror Rust behavior.
    // - We only verify that destruction should not hang or crash.

    std::thread* t;
    {
        auto pm = Mutex::make();

        std::atomic<bool> locked { false };
        std::atomic<bool> proceed { false };

        t = new std::thread([&] {
            pm.lock();
            locked.store(true, std::memory_order_release);
            while (! proceed.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            // Note: we intentionally do not unlock here, keeping the mutex locked,
            // then the main thread destroys the object to trigger destructor logic.
            // The thread does not access pm before exiting (avoid use-after-free).
        });

        while (! locked.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        // Allow the thread to continue to the "locked but no longer touching pm" state
        proceed.store(true, std::memory_order_release);

        // Yield briefly to help ensure t has passed the while and no longer reads pm
        std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // Trigger destruction: if the destructor is wrong (e.g., deadlock/hang), this will hang
    }

    t->join();
    delete t;
    SUCCEED();
}
#endif
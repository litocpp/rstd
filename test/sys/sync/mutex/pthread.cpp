#include <gtest/gtest.h>

import rstd;
using rstd::sys::sync::mutex::pthread::Mutex;

TEST(MutexPthread, LockUnlockBasic) {
    auto m = Mutex::make();

    m.lock();
    m.unlock();

    m.lock();
    m.unlock();

    SUCCEED();
}

TEST(MutexPthread, TryLockWhenUnlocked) {
    auto m = Mutex::make();

    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

TEST(MutexPthread, TryLockFailsWhenLockedByOtherThread) {
    auto m = Mutex::make();

    std::atomic<bool> locked { false };
    std::atomic<bool> stop { false };

    std::thread t([&] {
        m.lock();
        locked.store(true, std::memory_order_release);
        while (! stop.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        m.unlock();
    });

    // Wait for the other thread to acquire the lock
    while (! locked.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    EXPECT_FALSE(m.try_lock()); // Should fail
    stop.store(true, std::memory_order_release);
    t.join();
}

TEST(MutexPthread, MutualExclusionCounter) {
    auto m = Mutex::make();

    constexpr int kThreads = 8;
    constexpr int kIters   = 50'000;

    int counter = 0;

    auto worker = [&] {
        for (int i = 0; i < kIters; ++i) {
            m.lock();
            ++counter;
            m.unlock();
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) threads.emplace_back(worker);
    for (auto& th : threads) th.join();

    EXPECT_EQ(counter, kThreads * kIters);
}

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
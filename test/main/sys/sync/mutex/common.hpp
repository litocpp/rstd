

TEST(RSTD_TEST_GROUP, LockUnlockBasic) {
    auto m = Mutex::make();

    m.lock();
    m.unlock();

    m.lock();
    m.unlock();

    SUCCEED();
}

TEST(RSTD_TEST_GROUP, TryLockWhenUnlocked) {
    auto m = Mutex::make();

    EXPECT_TRUE(m.try_lock());
    m.unlock();
}

TEST(RSTD_TEST_GROUP, TryLockFailsWhenLockedByOtherThread) {
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

TEST(RSTD_TEST_GROUP, MutualExclusionCounter) {
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
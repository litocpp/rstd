#include <gtest/gtest.h>

import rstd;

using namespace rstd;

TEST(ThreadPool, RejectsZeroWorkers) {
    EXPECT_TRUE(thread::ThreadPoolBuilder::make().worker_count(usize()).build().is_err());
}

TEST(ThreadPool, ReusesWorkersAndDrainsAcceptedJobs) {
    auto pool   = thread::ThreadPoolBuilder::make().worker_count(usize(2)).build().unwrap();
    auto handle = pool.handle();
    sync::Mutex<::alloc::vec::Vec<thread::ThreadId>> worker_ids(
        ::alloc::vec::Vec<thread::ThreadId>::make());
    std::atomic<int> completed { 0 };

    for (int index = 0; index < 16; ++index) {
        ASSERT_TRUE(handle
                        .post([&] {
                            auto ids  = worker_ids.lock().unwrap_unchecked();
                            auto id   = thread::current().id();
                            bool seen = false;
                            for (const auto& existing : *ids) {
                                if (existing == id) seen = true;
                            }
                            if (! seen) ids->push(rstd::move(id));
                            completed.fetch_add(1);
                        })
                        .is_ok());
    }

    rstd::move(pool).join();
    EXPECT_EQ(completed.load(), 16);
    auto ids = worker_ids.lock().unwrap_unchecked();
    EXPECT_GE(ids->len(), usize(1));
    EXPECT_LE(ids->len(), usize(2));
    EXPECT_TRUE(handle.is_closed());
}

TEST(ThreadPool, CancelPendingKeepsRunningJobs) {
    auto pool   = thread::ThreadPoolBuilder::make().worker_count(usize(1)).build().unwrap();
    auto handle = pool.handle();
    std::atomic<bool> entered { false };
    std::atomic<bool> release { false };
    std::atomic<int>  completed { 0 };

    ASSERT_TRUE(handle
                    .post([&] {
                        entered.store(true);
                        while (! release.load()) thread::yield_now();
                        completed.fetch_add(1);
                    })
                    .is_ok());
    while (! entered.load()) thread::yield_now();
    ASSERT_TRUE(handle
                    .post([&] {
                        completed.fetch_add(10);
                    })
                    .is_ok());

    EXPECT_EQ(pool.cancel_pending(), usize(1));
    release.store(true);
    rstd::move(pool).join();
    EXPECT_EQ(completed.load(), 1);
    EXPECT_TRUE(handle.is_closed());
    EXPECT_TRUE(handle
                    .post([] {
                    })
                    .is_err());
}

TEST(ThreadPool, SupportsConcurrentProducers) {
    auto pool   = thread::ThreadPoolBuilder::make().worker_count(usize(3)).build().unwrap();
    auto handle = pool.handle();
    std::atomic<int> completed { 0 };
    auto             producers = ::alloc::vec::Vec<thread::JoinHandle<void>>::make();

    for (int producer = 0; producer < 4; ++producer) {
        auto producer_handle = handle.clone();
        producers.push(
            thread::spawn([producer_handle = rstd::move(producer_handle), &completed]() mutable {
                for (int index = 0; index < 25; ++index) {
                    EXPECT_TRUE(producer_handle
                                    .post([&completed] {
                                        completed.fetch_add(1);
                                    })
                                    .is_ok());
                }
            }).unwrap());
    }
    while (! producers.is_empty()) {
        rstd::move(producers.pop().unwrap_unchecked()).join().unwrap();
    }

    rstd::move(pool).join();
    EXPECT_EQ(completed.load(), 100);
}

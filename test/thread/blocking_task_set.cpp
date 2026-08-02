#include <rstd/test/gtest.hpp>
#include <atomic>

import rstd;

using namespace rstd;

TEST(BlockingTaskSet, RejectsInvalidConfiguration) {
    auto pool = thread::ThreadPoolBuilder::make().worker_count(usize(1)).build().unwrap();
    EXPECT_TRUE(thread::BlockingTaskSet<int>::make(pool.handle(), usize()).is_err());
}

TEST(BlockingTaskSet, ReceivesResultsInCompletionOrder) {
    auto pool  = thread::ThreadPoolBuilder::make().worker_count(usize(2)).build().unwrap();
    auto tasks = thread::BlockingTaskSet<int>::make(pool.handle(), usize(2)).unwrap();
    std::atomic<bool> first_entered { false };
    std::atomic<bool> release_first { false };

    ASSERT_TRUE(tasks
                    .try_submit([&] {
                        first_entered.store(true);
                        while (! release_first.load()) thread::yield_now();
                        return 10;
                    })
                    .is_ok());
    while (! first_entered.load()) thread::yield_now();
    ASSERT_TRUE(tasks
                    .try_submit([] {
                        return 20;
                    })
                    .is_ok());

    auto second = tasks.recv().unwrap();
    EXPECT_EQ(second.id(), usize(1));
    EXPECT_EQ(*second.value(), 20);

    release_first.store(true);
    auto first = tasks.recv().unwrap();
    EXPECT_EQ(first.id(), usize());
    EXPECT_EQ(*first.value(), 10);

    tasks.close();
    EXPECT_TRUE(tasks.recv().is_none());
    rstd::move(pool).join();
}

TEST(BlockingTaskSet, BoundsUnconsumedResults) {
    auto pool  = thread::ThreadPoolBuilder::make().worker_count(usize(1)).build().unwrap();
    auto tasks = thread::BlockingTaskSet<int>::make(pool.handle(), usize(2)).unwrap();
    std::atomic<bool> release { false };

    ASSERT_TRUE(tasks
                    .try_submit([&] {
                        while (! release.load()) thread::yield_now();
                        return 1;
                    })
                    .is_ok());
    ASSERT_TRUE(tasks
                    .try_submit([] {
                        return 2;
                    })
                    .is_ok());
    auto full = tasks.try_submit([] {
        return 3;
    });
    ASSERT_TRUE(full.is_err());
    EXPECT_EQ(full.unwrap_err_unchecked(), thread::BlockingTaskSetSubmitError::Full);

    release.store(true);
    EXPECT_TRUE(tasks.recv().is_some());
    EXPECT_TRUE(tasks
                    .try_submit([] {
                        return 3;
                    })
                    .is_ok());
    tasks.close();
    EXPECT_TRUE(tasks.recv().is_some());
    EXPECT_TRUE(tasks.recv().is_some());
    EXPECT_TRUE(tasks.recv().is_none());
    rstd::move(pool).join();
}

TEST(BlockingTaskSet, CancelsTasksThatHaveNotStarted) {
    auto pool  = thread::ThreadPoolBuilder::make().worker_count(usize(1)).build().unwrap();
    auto tasks = thread::BlockingTaskSet<int>::make(pool.handle(), usize(2)).unwrap();
    std::atomic<bool> entered { false };
    std::atomic<bool> release { false };

    ASSERT_TRUE(tasks
                    .try_submit([&] {
                        entered.store(true);
                        while (! release.load()) thread::yield_now();
                        return 1;
                    })
                    .is_ok());
    while (! entered.load()) thread::yield_now();
    ASSERT_TRUE(tasks
                    .try_submit([] {
                        return 2;
                    })
                    .is_ok());

    tasks.cancel_pending();
    release.store(true);
    auto first  = tasks.recv().unwrap();
    auto second = tasks.recv().unwrap();
    EXPECT_TRUE(first.is_completed());
    EXPECT_FALSE(first.is_cancelled());
    EXPECT_TRUE(second.is_cancelled());
    EXPECT_FALSE(second.is_completed());
    EXPECT_TRUE(tasks.recv().is_none());
    rstd::move(pool).join();
}

TEST(BlockingTaskSet, RollsBackSubmissionRejectedByPool) {
    auto pool  = thread::ThreadPoolBuilder::make().worker_count(usize(1)).build().unwrap();
    auto tasks = thread::BlockingTaskSet<int>::make(pool.handle(), usize(1)).unwrap();
    pool.close();

    auto submitted = tasks.try_submit([] {
        return 1;
    });
    ASSERT_TRUE(submitted.is_err());
    EXPECT_EQ(submitted.unwrap_err_unchecked(), thread::BlockingTaskSetSubmitError::Closed);
    tasks.close();
    EXPECT_TRUE(tasks.recv().is_none());
    rstd::move(pool).join();
}

TEST(BlockingTaskSet, ReturnsMoveOnlyResults) {
    auto pool  = thread::ThreadPoolBuilder::make().worker_count(usize(1)).build().unwrap();
    auto tasks = thread::BlockingTaskSet<boxed::Box<int>>::make(pool.handle(), usize(1)).unwrap();

    ASSERT_TRUE(tasks
                    .try_submit([] {
                        return boxed::Box<int>::make(42);
                    })
                    .is_ok());
    auto completion = tasks.recv().unwrap();
    auto value      = rstd::move(completion).into_value().unwrap();
    EXPECT_EQ(*value, 42);

    tasks.close();
    rstd::move(pool).join();
}

TEST(BlockingTaskSet, SharesPoolWithoutCrossCancellation) {
    auto pool  = thread::ThreadPoolBuilder::make().worker_count(usize(2)).build().unwrap();
    auto group = thread::BlockingTaskGroup<int>::on(pool.handle(), usize(1), usize(1)).unwrap();
    auto tasks = thread::BlockingTaskSet<int>::make(pool.handle(), usize(1)).unwrap();
    std::atomic<bool> entered { false };
    std::atomic<bool> release { false };

    ASSERT_TRUE(group
                    .submit([&] {
                        entered.store(true);
                        while (! release.load()) thread::yield_now();
                        return 1;
                    })
                    .is_ok());
    while (! entered.load()) thread::yield_now();
    ASSERT_TRUE(tasks
                    .try_submit([] {
                        return 2;
                    })
                    .is_ok());
    EXPECT_EQ(*tasks.recv().unwrap().value(), 2);
    tasks.cancel_pending();

    release.store(true);
    auto outcomes = rstd::move(group).join();
    ASSERT_EQ(outcomes.len(), usize(1));
    EXPECT_EQ(*outcomes[usize()].value(), 1);
    rstd::move(pool).join();
}

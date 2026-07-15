#include <gtest/gtest.h>

import rstd;

using namespace rstd;

TEST(BlockingTaskGroup, RejectsInvalidConfiguration) {
    EXPECT_TRUE(thread::BlockingTaskGroup<int>::make(0, 1).is_err());
    EXPECT_TRUE(thread::BlockingTaskGroup<int>::make(1, 0).is_err());
}

TEST(BlockingTaskGroup, PreservesSubmissionOrderWithBoundedConcurrency) {
    auto             group = thread::BlockingTaskGroup<int>::make(3, 4).unwrap();
    std::atomic<int> active { 0 };
    std::atomic<int> maximum { 0 };

    for (int index = 0; index < 8; ++index) {
        auto submitted = group.submit([index, &active, &maximum] {
            auto current  = active.fetch_add(1) + 1;
            auto observed = maximum.load();
            while (observed < current && ! maximum.compare_exchange_weak(observed, current)) {
            }
            thread::sleep(time::Duration::from_millis(static_cast<u64>((8 - index) * 2)));
            active.fetch_sub(1);
            return index * 10;
        });
        ASSERT_TRUE(submitted.is_ok());
        EXPECT_EQ(submitted.unwrap_unchecked(), static_cast<usize>(index));
    }

    auto outcomes = rstd::move(group).join();
    ASSERT_EQ(outcomes.len(), 8u);
    EXPECT_LE(maximum.load(), 3);
    EXPECT_GT(maximum.load(), 1);
    for (usize index = 0; index < outcomes.len(); ++index) {
        EXPECT_TRUE(outcomes[index].is_completed());
        EXPECT_FALSE(outcomes[index].is_cancelled());
        EXPECT_EQ(*outcomes[index].value(), static_cast<int>(index) * 10);
    }
}

TEST(BlockingTaskGroup, CancelWakesBlockedSubmitAndJoinsRunningTask) {
    auto              group         = thread::BlockingTaskGroup<int>::make(1, 1).unwrap();
    auto              cancel_handle = group.cancel_handle();
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
    ASSERT_TRUE(group
                    .submit([] {
                        return 2;
                    })
                    .is_ok());

    std::atomic<bool> submit_finished { false };
    auto              submitter =
        thread::spawn([&] {
            auto result = group.submit([] {
                return 3;
            });
            EXPECT_TRUE(result.is_err());
            EXPECT_EQ(result.unwrap_err_unchecked(), thread::BlockingSubmitError::Cancelled);
            submit_finished.store(true);
        }).unwrap();

    thread::sleep(time::Duration::from_millis(5));
    EXPECT_FALSE(submit_finished.load());
    EXPECT_FALSE(cancel_handle.is_cancelled());
    EXPECT_EQ(cancel_handle.cancel_pending(), 1u);
    EXPECT_TRUE(cancel_handle.is_cancelled());
    release.store(true);
    rstd::move(submitter).join().unwrap();

    auto outcomes = rstd::move(group).join();
    ASSERT_EQ(outcomes.len(), 2u);
    EXPECT_TRUE(outcomes[0].is_completed());
    EXPECT_EQ(*outcomes[0].value(), 1);
    EXPECT_TRUE(outcomes[1].is_cancelled());
    EXPECT_FALSE(outcomes[1].is_completed());
}

TEST(BlockingTaskGroup, CloseDrainsAcceptedTasksAndRejectsNewOnes) {
    auto group = thread::BlockingTaskGroup<int>::make(2, 2).unwrap();
    ASSERT_TRUE(group
                    .submit([] {
                        return 4;
                    })
                    .is_ok());
    ASSERT_TRUE(group
                    .submit([] {
                        return 5;
                    })
                    .is_ok());
    group.close();

    auto rejected = group.submit([] {
        return 6;
    });
    ASSERT_TRUE(rejected.is_err());
    EXPECT_EQ(rejected.unwrap_err_unchecked(), thread::BlockingSubmitError::Closed);

    auto outcomes = rstd::move(group).join();
    ASSERT_EQ(outcomes.len(), 2u);
    EXPECT_EQ(*outcomes[0].value(), 4);
    EXPECT_EQ(*outcomes[1].value(), 5);
}

TEST(BlockingTaskGroup, DestructorCancelsQueuedAndJoinsRunningTask) {
    std::atomic<bool> entered { false };
    std::atomic<bool> release { false };
    std::atomic<bool> completed { false };

    auto releaser = thread::spawn([&] {
                        while (! entered.load()) thread::yield_now();
                        thread::sleep(time::Duration::from_millis(5));
                        release.store(true);
                    }).unwrap();

    {
        auto group = thread::BlockingTaskGroup<int>::make(1, 1).unwrap();
        ASSERT_TRUE(group
                        .submit([&] {
                            entered.store(true);
                            while (! release.load()) thread::yield_now();
                            completed.store(true);
                            return 1;
                        })
                        .is_ok());
        while (! entered.load()) thread::yield_now();
        ASSERT_TRUE(group
                        .submit([] {
                            return 2;
                        })
                        .is_ok());
    }

    rstd::move(releaser).join().unwrap();
    EXPECT_TRUE(completed.load());
}

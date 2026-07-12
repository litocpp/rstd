#include <gtest/gtest.h>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

auto await_completion(async::Completion<int> completion, std::atomic<bool>& entered)
    -> async::coro<Result<int, async::CompletionError<empty>>> {
    entered.store(true, std::memory_order_release);
    co_return co_await rstd::move(completion);
}

auto consume_closed_queue(async::CompletionQueue<int> queue) -> async::coro<int> {
    auto first = co_await queue.next();
    if (first.is_err()) {
        co_return -1;
    }
    auto first_value = rstd::move(first).unwrap_unchecked();
    if (first_value.is_none()) {
        co_return -2;
    }

    auto second = co_await queue.next();
    if (second.is_err()) {
        co_return -3;
    }
    auto second_value = rstd::move(second).unwrap_unchecked();
    if (second_value.is_none()) {
        co_return -4;
    }

    auto end = co_await queue.next();
    if (end.is_err() || rstd::move(end).unwrap_unchecked().is_some()) {
        co_return -5;
    }
    co_return *first_value + *second_value;
}

auto await_executor_once(async::AnyExecutor executor, std::atomic<u64>& thread_id)
    -> async::coro<bool> {
    auto accepted = co_await executor;
    thread_id.store(thread::current().id().as_u64().get(), std::memory_order_release);
    co_return accepted;
}

auto await_executor_twice(async::AnyExecutor executor, std::atomic<int>& continuations)
    -> async::coro<bool> {
    if (! co_await executor) {
        co_return false;
    }
    continuations.fetch_add(1, std::memory_order_relaxed);
    if (! co_await executor) {
        co_return false;
    }
    continuations.fetch_add(1, std::memory_order_relaxed);
    co_return true;
}

auto sleep_and_mark(std::atomic<int>& completions) -> async::coro<void> {
    co_await async::sleep(time::Duration::from_millis(1));
    completions.fetch_add(1, std::memory_order_relaxed);
}

auto long_sleep() -> async::coro<void> {
    co_await async::sleep(time::Duration::from_millis(60'000));
}

template<typename T>
auto run_executor_until_finished(async::LocalExecutorContext& context,
                                 async::JoinHandle<T> const&  handle) -> usize {
    auto ran = usize {};
    for (int attempt = 0; attempt < 1000 && ! handle.is_finished(); ++attempt) {
        ran += context.run_ready();
        if (! handle.is_finished()) {
            thread::sleep(time::Duration::from_millis(1));
        }
    }
    return ran;
}

TEST(RstdAsyncFacility, CompletionPublishesOnlyFirstTerminalValue) {
    auto pair       = async::Completion<int>::make().unwrap();
    auto completion = rstd::move(pair.get<0>());
    auto handle     = rstd::move(pair.get<1>());

    EXPECT_TRUE(handle.complete(5).is_ok());
    auto duplicate = handle.complete(9);
    ASSERT_TRUE(duplicate.is_err());
    EXPECT_EQ(rstd::move(duplicate).unwrap_err(), 9);

    auto result = async::block_on(rstd::move(completion));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap(), 5);
}

TEST(RstdAsyncFacility, CompletionFromAnotherThreadWakesCurrentRuntime) {
    auto pair       = async::Completion<int>::make().unwrap();
    auto completion = rstd::move(pair.get<0>());
    auto handle     = rstd::move(pair.get<1>());
    auto entered    = std::atomic<bool> { false };

    auto producer = thread::spawn([handle = rstd::move(handle), &entered]() mutable {
                        while (! entered.load(std::memory_order_acquire)) {
                            hint::spin_loop();
                        }
                        thread::sleep(time::Duration::from_millis(1));
                        return handle.complete(23).is_ok();
                    }).unwrap();

    auto result = async::block_on(await_completion(rstd::move(completion), entered));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap(), 23);
    EXPECT_TRUE(rstd::move(producer).join().unwrap());
}

TEST(RstdAsyncFacility, LastCompletionHandleDropPublishesCanceled) {
    auto pair       = async::Completion<int>::make().unwrap();
    auto completion = rstd::move(pair.get<0>());
    {
        auto handle = rstd::move(pair.get<1>());
    }

    auto result = async::block_on(rstd::move(completion));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err().is_canceled());
}

TEST(RstdAsyncFacility, ClosedCompletionQueueDrainsOwnedItemsInOrder) {
    auto pair     = async::CompletionQueue<int>::make().unwrap();
    auto queue    = rstd::move(pair.get<0>());
    auto producer = rstd::move(pair.get<1>());

    ASSERT_TRUE(producer.push(4).is_ok());
    ASSERT_TRUE(producer.push(8).is_ok());
    producer.close();

    EXPECT_EQ(async::block_on(consume_closed_queue(rstd::move(queue))), 12);
}

TEST(RstdAsyncFacility, ExecutorJobConsumesRunOrCancelExactlyOnce) {
    int  runs    = 0;
    int  cancels = 0;
    auto run_job = async::ExecutorJob::make(
        [&runs] {
            ++runs;
        },
        [&cancels] {
            ++cancels;
        });

    run_job.run();
    run_job.run();
    run_job.cancel();
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(cancels, 0);

    {
        auto cancel_job = async::ExecutorJob::make(
            [&runs] {
                ++runs;
            },
            [&cancels] {
                ++cancels;
            });
    }
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(cancels, 1);
}

TEST(RstdAsyncFacility, ClosedExecutorDrainsAcceptedJobAndRejectsNewJob) {
    auto context  = async::LocalExecutorContext::make();
    auto executor = context.executor();
    int  runs     = 0;
    int  cancels  = 0;

    EXPECT_TRUE(executor.post([&runs] {
        ++runs;
    }));
    context.close();
    EXPECT_FALSE(executor.post_job(async::ExecutorJob::make(
        [] {
        },
        [&cancels] {
            ++cancels;
        })));

    EXPECT_EQ(cancels, 1);
    EXPECT_EQ(context.run_ready(), usize { 1 });
    EXPECT_EQ(runs, 1);
}

TEST(RstdAsyncFacility, ExecutorAwaitUsesOneExternalJob) {
    auto context   = async::LocalExecutorContext::make();
    auto executor  = async::AnyExecutor::from_executor(context.executor());
    auto runtime   = async::RuntimeBuilder::multi_thread().worker_threads(1).build().unwrap();
    auto thread_id = std::atomic<u64> { 0 };
    auto caller_id = thread::current().id().as_u64().get();
    auto joined    = runtime.spawn(await_executor_once(executor.clone(), thread_id));

    auto ran = run_executor_until_finished(context, joined);
    ASSERT_TRUE(joined.is_finished());
    auto out = runtime.block_on(rstd::move(joined));

    ASSERT_TRUE(out.is_ok());
    EXPECT_TRUE(rstd::move(out).unwrap());
    EXPECT_EQ(ran, usize { 1 });
    EXPECT_EQ(thread_id.load(std::memory_order_acquire), caller_id);
}

TEST(RstdAsyncFacility, ConsecutiveExecutorAwaitsIssueDistinctJobs) {
    auto context       = async::LocalExecutorContext::make();
    auto executor      = async::AnyExecutor::from_executor(context.executor());
    auto runtime       = async::RuntimeBuilder::multi_thread().worker_threads(1).build().unwrap();
    auto continuations = std::atomic<int> { 0 };
    auto joined        = runtime.spawn(await_executor_twice(executor.clone(), continuations));

    auto ran = run_executor_until_finished(context, joined);
    ASSERT_TRUE(joined.is_finished());
    auto out = runtime.block_on(rstd::move(joined));

    ASSERT_TRUE(out.is_ok());
    EXPECT_TRUE(rstd::move(out).unwrap());
    EXPECT_EQ(ran, usize { 2 });
    EXPECT_EQ(continuations.load(std::memory_order_relaxed), 2);
}

TEST(RstdAsyncFacility, ClosedExecutorRejectsWithoutPostingJob) {
    auto context  = async::LocalExecutorContext::make();
    auto executor = async::AnyExecutor::from_executor(context.executor());
    context.close();
    auto thread_id = std::atomic<u64> { 0 };

    EXPECT_FALSE(async::block_on(await_executor_once(executor.clone(), thread_id)));
    EXPECT_EQ(context.run_ready(), usize { 0 });
}

TEST(RstdAsyncFacility, TimerCompletionReturnsThroughRuntimeQueue) {
    auto completions = std::atomic<int> { 0 };
    auto runtime     = async::RuntimeBuilder::current_thread().enable_time().build().unwrap();

    runtime.block_on(sleep_and_mark(completions));
    EXPECT_EQ(completions.load(std::memory_order_relaxed), 1);
}

TEST(RstdAsyncFacility, RuntimeShutdownCancelsPendingTimer) {
    auto joined = [] {
        auto runtime =
            async::RuntimeBuilder::multi_thread().worker_threads(1).enable_time().build().unwrap();
        return runtime.spawn(long_sleep());
    }();

    EXPECT_TRUE(joined.is_finished());
    auto result = async::block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err().is_aborted());
}

} // namespace

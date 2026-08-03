#include <rstd/test/gtest.hpp>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct MoveOnlyValue {
    int value;

    explicit MoveOnlyValue(int value): value(value) {}

    MoveOnlyValue(const MoveOnlyValue&)                        = delete;
    auto operator=(const MoveOnlyValue&) -> MoveOnlyValue&     = delete;
    MoveOnlyValue(MoveOnlyValue&&) noexcept                    = default;
    auto operator=(MoveOnlyValue&&) noexcept -> MoveOnlyValue& = default;
};

auto free_spawn_blocking(thread::ThreadId async_thread) -> async::coro<bool> {
    auto submitted = async::spawn_blocking([async_thread] {
        return MoveOnlyValue { thread::current().id() == async_thread ? -1 : 41 };
    });
    if (submitted.is_err()) co_return false;
    auto joined = co_await rstd::move(submitted).unwrap_unchecked();
    co_return joined.is_ok() && rstd::move(joined).unwrap_unchecked().value == 41;
}

auto blocking_jobs_keep_async_worker_responsive(std::atomic<bool>& entered,
                                                std::atomic<bool>& release) -> async::coro<bool> {
    auto first = async::spawn_blocking([&] {
        entered.store(true, std::memory_order_release);
        while (! release.load(std::memory_order_acquire)) hint::spin_loop();
        return 19;
    });
    if (first.is_err()) co_return false;
    while (! entered.load(std::memory_order_acquire)) co_await async::yield_now();

    auto second = async::spawn_blocking([] {
        return 23;
    });
    if (second.is_err()) {
        release.store(true, std::memory_order_release);
        co_return false;
    }
    co_await async::yield_now();
    release.store(true, std::memory_order_release);
    auto first_result  = co_await rstd::move(first).unwrap_unchecked();
    auto second_result = co_await rstd::move(second).unwrap_unchecked();
    co_return first_result.is_ok() && first_result.unwrap_unchecked() == 19 &&
        second_result.is_ok() && second_result.unwrap_unchecked() == 23;
}

TEST(RstdAsyncSpawnBlocking, FreeFunctionRunsOffCurrentThreadWithoutIoDriver) {
    auto runtime = async::RuntimeBuilder::current_thread().build().unwrap();
    EXPECT_TRUE(runtime.block_on(free_spawn_blocking(thread::current().id())));
}

TEST(RstdAsyncSpawnBlocking, RuntimeAndHandleReturnMoveOnlyValues) {
    auto runtime = async::RuntimeBuilder::multi_thread()
                       .worker_threads(usize(1))
                       .max_blocking_threads(usize(2))
                       .build()
                       .unwrap();
    auto direct  = runtime.spawn_blocking([] {
        return MoveOnlyValue { 17 };
    });
    ASSERT_TRUE(direct.is_ok());
    auto direct_result = runtime.block_on(rstd::move(direct).unwrap_unchecked());
    ASSERT_TRUE(direct_result.is_ok());
    EXPECT_EQ(rstd::move(direct_result).unwrap_unchecked().value, 17);

    auto handle = runtime.handle();
    auto remote = handle.spawn_blocking([] {
        return MoveOnlyValue { 29 };
    });
    ASSERT_TRUE(remote.is_ok());
    auto remote_result = runtime.block_on(rstd::move(remote).unwrap_unchecked());
    ASSERT_TRUE(remote_result.is_ok());
    EXPECT_EQ(rstd::move(remote_result).unwrap_unchecked().value, 29);

    auto ran   = false;
    auto empty = runtime.spawn_blocking([&] {
        ran = true;
    });
    ASSERT_TRUE(empty.is_ok());
    auto empty_result = runtime.block_on(rstd::move(empty).unwrap_unchecked());
    EXPECT_TRUE(empty_result.is_ok());
    EXPECT_TRUE(ran);
}

TEST(RstdAsyncSpawnBlocking, RuntimeHandleSubmitsFromExternalThread) {
    auto runtime   = async::RuntimeBuilder::current_thread().build().unwrap();
    auto submitter = thread::spawn([handle = runtime.handle()]() mutable {
        return handle.spawn_blocking([] {
            return MoveOnlyValue { 31 };
        });
    });
    ASSERT_TRUE(submitter.is_ok());
    auto submitted = rstd::move(submitter).unwrap_unchecked().join();
    ASSERT_TRUE(submitted.is_ok());
    auto blocking = rstd::move(submitted).unwrap_unchecked();
    ASSERT_TRUE(blocking.is_ok());
    auto result = runtime.block_on(rstd::move(blocking).unwrap_unchecked());
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap_unchecked().value, 31);
}

TEST(RstdAsyncSpawnBlocking, BlockingAndQueuedJobsDoNotStallAsyncWorker) {
    auto entered = std::atomic<bool> { false };
    auto release = std::atomic<bool> { false };
    auto runtime =
        async::RuntimeBuilder::current_thread().max_blocking_threads(usize(1)).build().unwrap();
    EXPECT_TRUE(runtime.block_on(blocking_jobs_keep_async_worker_responsive(entered, release)));
}

TEST(RstdAsyncSpawnBlocking, QueuedAbortSkipsClosure) {
    auto runtime =
        async::RuntimeBuilder::current_thread().max_blocking_threads(usize(1)).build().unwrap();
    auto entered = std::atomic<bool> { false };
    auto release = std::atomic<bool> { false };
    auto ran     = std::atomic<bool> { false };

    auto first = runtime.spawn_blocking([&] {
        entered.store(true, std::memory_order_release);
        while (! release.load(std::memory_order_acquire)) hint::spin_loop();
    });
    ASSERT_TRUE(first.is_ok());
    while (! entered.load(std::memory_order_acquire)) hint::spin_loop();

    auto second = runtime.spawn_blocking([&] {
        ran.store(true, std::memory_order_release);
        return 5;
    });
    if (second.is_err()) {
        release.store(true, std::memory_order_release);
        FAIL() << "second blocking job was rejected";
        return;
    }
    auto second_handle = rstd::move(second).unwrap_unchecked();
    second_handle.abort();
    release.store(true, std::memory_order_release);

    auto first_result = runtime.block_on(rstd::move(first).unwrap_unchecked());
    EXPECT_TRUE(first_result.is_ok());
    auto second_result = runtime.block_on(rstd::move(second_handle));
    ASSERT_TRUE(second_result.is_err());
    EXPECT_TRUE(rstd::move(second_result).unwrap_err_unchecked().is_aborted());
    EXPECT_FALSE(ran.load(std::memory_order_acquire));
}

TEST(RstdAsyncSpawnBlocking, RunningAbortDoesNotOverwriteValue) {
    auto runtime =
        async::RuntimeBuilder::current_thread().max_blocking_threads(usize(1)).build().unwrap();
    auto entered   = std::atomic<bool> { false };
    auto release   = std::atomic<bool> { false };
    auto submitted = runtime.spawn_blocking([&] {
        entered.store(true, std::memory_order_release);
        while (! release.load(std::memory_order_acquire)) hint::spin_loop();
        return 37;
    });
    ASSERT_TRUE(submitted.is_ok());
    auto handle = rstd::move(submitted).unwrap_unchecked();
    while (! entered.load(std::memory_order_acquire)) hint::spin_loop();
    handle.abort();
    release.store(true, std::memory_order_release);

    auto result = runtime.block_on(rstd::move(handle));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap_unchecked(), 37);
}

TEST(RstdAsyncSpawnBlocking, IdleWorkerIsReused) {
    auto runtime =
        async::RuntimeBuilder::current_thread().max_blocking_threads(usize(1)).build().unwrap();
    auto first = runtime.spawn_blocking([] {
        return thread::current().id();
    });
    ASSERT_TRUE(first.is_ok());
    auto first_result = runtime.block_on(rstd::move(first).unwrap_unchecked());
    ASSERT_TRUE(first_result.is_ok());

    auto second = runtime.spawn_blocking([] {
        return thread::current().id();
    });
    ASSERT_TRUE(second.is_ok());
    auto second_result = runtime.block_on(rstd::move(second).unwrap_unchecked());
    ASSERT_TRUE(second_result.is_ok());
    EXPECT_EQ(rstd::move(first_result).unwrap_unchecked(),
              rstd::move(second_result).unwrap_unchecked());
}

TEST(RstdAsyncSpawnBlocking, RuntimeShutdownCancelsQueuedAndWaitsForRunning) {
    auto release    = std::atomic<bool> { false };
    auto entered    = std::atomic<bool> { false };
    auto queued_ran = std::atomic<bool> { false };
    auto runtime =
        async::RuntimeBuilder::current_thread().max_blocking_threads(usize(1)).build().unwrap();
    auto first = runtime.spawn_blocking([&] {
        entered.store(true, std::memory_order_release);
        while (! release.load(std::memory_order_acquire)) hint::spin_loop();
        return 11;
    });
    ASSERT_TRUE(first.is_ok());
    while (! entered.load(std::memory_order_acquire)) hint::spin_loop();
    auto second = runtime.spawn_blocking([&] {
        queued_ran.store(true, std::memory_order_release);
        return 13;
    });
    if (second.is_err()) {
        release.store(true, std::memory_order_release);
        FAIL() << "queued blocking job was rejected";
        return;
    }
    auto first_handle  = rstd::move(first).unwrap_unchecked();
    auto second_handle = rstd::move(second).unwrap_unchecked();

    auto destroyer_result = thread::spawn([runtime = rstd::move(runtime)]() mutable {
        (void)runtime.io_enabled();
    });
    ASSERT_TRUE(destroyer_result.is_ok());
    auto destroyer = rstd::move(destroyer_result).unwrap_unchecked();
    while (! second_handle.is_finished()) hint::spin_loop();
    release.store(true, std::memory_order_release);
    ASSERT_TRUE(rstd::move(destroyer).join().is_ok());

    auto first_result = async::block_on(rstd::move(first_handle));
    ASSERT_TRUE(first_result.is_ok());
    EXPECT_EQ(rstd::move(first_result).unwrap_unchecked(), 11);
    auto second_result = async::block_on(rstd::move(second_handle));
    ASSERT_TRUE(second_result.is_err());
    EXPECT_TRUE(rstd::move(second_result).unwrap_err_unchecked().is_aborted());
    EXPECT_FALSE(queued_ran.load(std::memory_order_acquire));
}

TEST(RstdAsyncSpawnBlocking, DroppingJoinHandleDetachesJob) {
    auto ran = std::atomic<bool> { false };
    {
        auto runtime   = async::RuntimeBuilder::current_thread().build().unwrap();
        auto submitted = runtime.spawn_blocking([&] {
            ran.store(true, std::memory_order_release);
        });
        ASSERT_TRUE(submitted.is_ok());
        (void)rstd::move(submitted).unwrap_unchecked();
        while (! ran.load(std::memory_order_acquire)) hint::spin_loop();
    }
    EXPECT_TRUE(ran.load(std::memory_order_acquire));
}

TEST(RstdAsyncSpawnBlocking, ExpiredRuntimeHandleRejectsSubmission) {
    auto handle = [&] {
        auto runtime = async::RuntimeBuilder::current_thread().build().unwrap();
        return runtime.handle();
    }();
    auto submitted = handle.spawn_blocking([] {
    });
    ASSERT_TRUE(submitted.is_err());
    EXPECT_EQ(rstd::move(submitted).unwrap_err_unchecked().kind(),
              io::error::ErrorKind { io::error::ErrorKind::NotConnected });
}

TEST(RstdAsyncSpawnBlocking, BuilderRejectsZeroBlockingThreads) {
    auto builder = async::RuntimeBuilder::current_thread();
    auto built   = builder.max_blocking_threads(usize()).build();
    ASSERT_TRUE(built.is_err());
    EXPECT_EQ(rstd::move(built).unwrap_err_unchecked().kind(),
              io::error::ErrorKind { io::error::ErrorKind::InvalidInput });
}

} // namespace

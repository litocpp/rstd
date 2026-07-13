#include <gtest/gtest.h>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct Pending {
    using Output = void;

    auto poll(mut_ref<Pending>, task::Context&) -> task::Poll<void> {
        return task::Poll<void>::Pending();
    }
};

struct DropFlag {
    std::atomic<bool>* dropped;

    ~DropFlag() { dropped->store(true, std::memory_order_release); }
};

auto pending_task(std::atomic<bool>& started, std::atomic<bool>& dropped) -> async::coro<void> {
    auto flag = DropFlag { &dropped };
    started.store(true, std::memory_order_release);
    co_await Pending {};
}

auto ready_value() -> async::coro<int> {
    co_return 17;
}

auto wait_for(std::atomic<bool>& value) -> bool {
    for (int i = 0; i < 1000; ++i) {
        if (value.load(std::memory_order_acquire)) return true;
        thread::sleep(time::Duration::from_millis(1));
    }
    return false;
}

TEST(RstdAsyncAbortOnDrop, AwaitsCompletedTask) {
    auto runtime = async::RuntimeBuilder::current_thread().build().unwrap();
    auto handle  = async::AbortOnDropHandle { runtime.spawn(ready_value()) };
    auto result  = runtime.block_on(rstd::move(handle));

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap(), 17);
}

TEST(RstdAsyncAbortOnDrop, DropAbortsPendingTaskAfterMoves) {
    auto runtime = async::RuntimeBuilder::multi_thread().worker_threads(1).build().unwrap();
    auto started = std::atomic<bool> { false };
    auto dropped = std::atomic<bool> { false };

    {
        auto first = async::AbortOnDropHandle { runtime.spawn(pending_task(started, dropped)) };
        ASSERT_TRUE(wait_for(started));
        auto second = rstd::move(first);
        auto third  = rstd::move(second);
        EXPECT_FALSE(third.is_finished());
    }

    EXPECT_TRUE(wait_for(dropped));
}

TEST(RstdAsyncAbortOnDrop, MoveAssignmentAbortsPreviousTask) {
    auto runtime       = async::RuntimeBuilder::multi_thread().worker_threads(1).build().unwrap();
    auto first_started = std::atomic<bool> { false };
    auto first_dropped = std::atomic<bool> { false };
    auto next_started  = std::atomic<bool> { false };
    auto next_dropped  = std::atomic<bool> { false };

    {
        auto first =
            async::AbortOnDropHandle { runtime.spawn(pending_task(first_started, first_dropped)) };
        auto next =
            async::AbortOnDropHandle { runtime.spawn(pending_task(next_started, next_dropped)) };

        ASSERT_TRUE(wait_for(first_started));
        ASSERT_TRUE(wait_for(next_started));
        first = rstd::move(next);

        EXPECT_TRUE(wait_for(first_dropped));
        EXPECT_FALSE(first.is_finished());
    }

    EXPECT_TRUE(wait_for(next_dropped));
}

TEST(RstdAsyncAbortOnDrop, DetachKeepsTaskRunning) {
    auto runtime = async::RuntimeBuilder::multi_thread().worker_threads(1).build().unwrap();
    auto started = std::atomic<bool> { false };
    auto dropped = std::atomic<bool> { false };
    auto guarded = async::AbortOnDropHandle { runtime.spawn(pending_task(started, dropped)) };

    ASSERT_TRUE(wait_for(started));
    auto detached = rstd::move(guarded).detach();

    EXPECT_FALSE(detached.is_finished());
    EXPECT_FALSE(dropped.load(std::memory_order_acquire));
    detached.abort();
    EXPECT_TRUE(wait_for(dropped));
}

} // namespace

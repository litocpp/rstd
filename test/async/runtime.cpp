#include <gtest/gtest.h>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct ReadyValue {
    using Output = int;

    int value;

    auto poll(mut_ref<ReadyValue> self, task::Context&) -> task::Poll<int> {
        return task::Poll<int>::Ready(self->value);
    }
};

struct DuplicateWakeValue {
    using Output = int;

    int* polls;

    auto poll(mut_ref<DuplicateWakeValue> self, task::Context& cx) -> task::Poll<int> {
        ++*self->polls;
        if (*self->polls == 1) {
            cx.waker().wake_by_ref();
            cx.waker().wake_by_ref();
            return task::Poll<int>::Pending();
        }
        return task::Poll<int>::Ready(29);
    }
};

struct CountedIntoFuture {
    using Future = ReadyValue;

    int* conversions;

    auto into_future() -> ReadyValue {
        ++*conversions;
        return ReadyValue { 17 };
    }
};

struct NeverReady {
    using Output = int;

    int* polls;

    auto poll(mut_ref<NeverReady> self, task::Context&) -> task::Poll<int> {
        ++*self->polls;
        return task::Poll<int>::Pending();
    }
};

static_assert(async::AwaitableInput<ReadyValue>);
static_assert(async::AwaitableInput<CountedIntoFuture>);
static_assert(mtp::same_as<async::await_output_t<ReadyValue>, int>);
static_assert(mtp::same_as<async::await_output_t<CountedIntoFuture>, int>);

auto lazy_value(int& starts) -> async::coro<int> {
    ++starts;
    co_return 11;
}

auto ordered_child(int& state) -> async::coro<int> {
    state = 2;
    co_await async::yield_now();
    state = 3;
    co_return 21;
}

auto ordered_parent(int& state) -> async::coro<int> {
    state      = 1;
    auto value = co_await ordered_child(state);
    if (state != 3) {
        co_return -1;
    }
    state = 4;
    co_return value * 2;
}

auto yield_once(int& state) -> async::coro<void> {
    state = 1;
    co_await async::yield_now();
    state = 2;
}

auto abort_queued_child(int& polls) -> async::coro<bool> {
    auto handle = async::spawn_local(NeverReady { &polls });
    handle.abort();
    auto result = co_await rstd::move(handle);
    co_return result.is_err() && result.unwrap_err().is_aborted();
}

auto thread_pool_child(std::atomic<int>& runs) -> async::coro<int> {
    runs.fetch_add(1, std::memory_order_relaxed);
    co_await async::yield_now();
    runs.fetch_add(1, std::memory_order_relaxed);
    co_return 31;
}

auto join_thread_pool_child(std::atomic<int>& runs) -> async::coro<int> {
    auto handle = async::spawn(thread_pool_child(runs));
    auto result = co_await rstd::move(handle);
    co_return result.unwrap();
}

TEST(RstdAsyncRuntime, ReadyFutureRunsThroughBlockOn) {
    EXPECT_EQ(async::block_on(ReadyValue { 7 }), 7);
}

TEST(RstdAsyncRuntime, DuplicateWakeProducesOneRunnableTicket) {
    int polls = 0;

    EXPECT_EQ(async::block_on(DuplicateWakeValue { &polls }), 29);
    EXPECT_EQ(polls, 2);
}

TEST(RstdAsyncRuntime, IntoFutureConvertsExactlyOnce) {
    int conversions = 0;

    EXPECT_EQ(async::block_on(CountedIntoFuture { &conversions }), 17);
    EXPECT_EQ(conversions, 1);
}

TEST(RstdAsyncRuntime, CoroutineStartsLazily) {
    int  starts = 0;
    auto task   = lazy_value(starts);

    EXPECT_EQ(starts, 0);
    EXPECT_EQ(async::block_on(rstd::move(task)), 11);
    EXPECT_EQ(starts, 1);
}

TEST(RstdAsyncRuntime, NestedCoroutineFinishesBeforeParentContinues) {
    int state = 0;

    EXPECT_EQ(async::block_on(ordered_parent(state)), 42);
    EXPECT_EQ(state, 4);
}

TEST(RstdAsyncRuntime, YieldRequeuesCurrentTask) {
    int state = 0;

    async::block_on(yield_once(state));
    EXPECT_EQ(state, 2);
}

TEST(RstdAsyncRuntime, AbortQueuedChildPublishesJoinErrorWithoutPolling) {
    int polls = 0;

    EXPECT_TRUE(async::block_on(abort_queued_child(polls)));
    EXPECT_EQ(polls, 0);
}

TEST(RstdAsyncRuntime, ThreadPoolSpawnAndJoinCompleteOnRuntime) {
    auto runs    = std::atomic<int> { 0 };
    auto runtime = async::RuntimeBuilder::multi_thread().worker_threads(2).build().unwrap();

    EXPECT_EQ(runtime.block_on(join_thread_pool_child(runs)), 31);
    EXPECT_EQ(runs.load(std::memory_order_relaxed), 2);
}

} // namespace

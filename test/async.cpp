#include <gtest/gtest.h>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct ReadyInt {
    using Output = int;

    auto poll(pin::Pin<mut_ref<ReadyInt>>, task::Context&) -> task::Poll<int> {
        return task::Poll<int>::Ready(7);
    }
};

struct TwoPollInt {
    using Output = int;

    int* polls;

    auto poll(pin::Pin<mut_ref<TwoPollInt>>, task::Context& cx) -> task::Poll<int> {
        ++*polls;
        if (*polls == 1) {
            cx.waker().wake_by_ref();
            return task::Poll<int>::Pending();
        }
        return task::Poll<int>::Ready(41);
    }
};

async::coro<int> lazy_value(int& counter) {
    ++counter;
    co_return 5;
}

async::coro<int> await_two_poll(int& polls) {
    auto value = co_await TwoPollInt { &polls };
    co_return value + 1;
}

async::coro<int> child_value() {
    co_await async::yield_now();
    co_return 21;
}

async::coro<int> join_child() {
    auto handle = async::spawn_local(child_value());
    auto result = co_await rstd::move(handle);
    co_return result.unwrap() * 2;
}

async::coro<void> sleep_once() {
    co_await async::sleep(time::Duration::from_millis(10));
}

coro<int> prelude_coro_value() {
    co_return 13;
}

struct WakerCounts {
    std::atomic<int> clones { 0 };
    std::atomic<int> wakes { 0 };
    std::atomic<int> wake_refs { 0 };
    std::atomic<int> drops { 0 };
};

auto count_clone(voidp data) -> voidp {
    auto* counts = static_cast<WakerCounts*>(data);
    ++counts->clones;
    return data;
}

void count_wake(voidp data) {
    ++static_cast<WakerCounts*>(data)->wakes;
}

void count_wake_by_ref(voidp data) {
    ++static_cast<WakerCounts*>(data)->wake_refs;
}

void count_drop(voidp data) {
    ++static_cast<WakerCounts*>(data)->drops;
}

const task::RawWakerVTable COUNT_WAKER_VTABLE {
    &count_clone,
    &count_wake,
    &count_wake_by_ref,
    &count_drop,
};

} // namespace

TEST(AsyncCoro, PollHelpers) {
    auto ready = task::Poll<int>::Ready(3);
    ASSERT_TRUE(ready.is_ready());
    EXPECT_EQ(rstd::move(ready).take(), 3);

    auto pending = task::Poll<int>::Pending();
    EXPECT_TRUE(pending.is_pending());
}

TEST(AsyncCoro, WakerOwnsRawHandle) {
    auto counts = WakerCounts {};
    {
        auto waker = task::Waker::from_raw(task::RawWaker { &counts, &COUNT_WAKER_VTABLE });
        auto clone = waker.clone();
        waker.wake_by_ref();
        rstd::move(clone).wake();
    }

    EXPECT_EQ(counts.clones.load(), 1);
    EXPECT_EQ(counts.wake_refs.load(), 1);
    EXPECT_EQ(counts.wakes.load(), 1);
    EXPECT_EQ(counts.drops.load(), 1);
}

TEST(AsyncCoro, BlockOnReadyFuture) {
    EXPECT_EQ(async::block_on(ReadyInt {}), 7);
}

TEST(AsyncCoro, CoroutineIsLazy) {
    int counter = 0;
    auto task   = lazy_value(counter);
    EXPECT_EQ(counter, 0);
    EXPECT_EQ(async::block_on(rstd::move(task)), 5);
    EXPECT_EQ(counter, 1);
}

TEST(AsyncCoro, CoroutineRePollsPendingChildAfterWake) {
    int polls = 0;
    EXPECT_EQ(async::block_on(await_two_poll(polls)), 42);
    EXPECT_EQ(polls, 2);
}

TEST(AsyncCoro, SpawnLocalJoinHandle) {
    EXPECT_EQ(async::block_on(join_child()), 42);
}

TEST(AsyncCoro, SleepWakesTask) {
    auto start = time::Instant::now();
    async::block_on(sleep_once());
    EXPECT_GE(start.elapsed().as_millis(), 5u);
}

TEST(AsyncCoro, PreludeExportsCoro) {
    EXPECT_EQ(async::block_on(prelude_coro_value()), 13);
}

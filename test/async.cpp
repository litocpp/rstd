#include <gtest/gtest.h>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;
using ::alloc::vec::Vec;

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

struct CountReadyInt {
    using Output = int;

    int* polls;
    int  value;

    auto poll(pin::Pin<mut_ref<CountReadyInt>>, task::Context&) -> task::Poll<int> {
        ++*polls;
        return task::Poll<int>::Ready(value);
    }
};

struct CountPendingInt {
    using Output = int;

    int* polls;
    int  value;

    auto poll(pin::Pin<mut_ref<CountPendingInt>>, task::Context& cx) -> task::Poll<int> {
        ++*polls;
        if (*polls == 1) {
            cx.waker().wake_by_ref();
            return task::Poll<int>::Pending();
        }
        return task::Poll<int>::Ready(value);
    }
};

struct IndexedPendingInt {
    using Output = int;

    int* polls;
    int  index;
    int  value;

    auto poll(pin::Pin<mut_ref<IndexedPendingInt>>, task::Context& cx) -> task::Poll<int> {
        ++polls[index];
        if (polls[index] == 1) {
            cx.waker().wake_by_ref();
            return task::Poll<int>::Pending();
        }
        return task::Poll<int>::Ready(value);
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

TEST(AsyncCoro, JoinPollsAllChildren) {
    int ready_polls  = 0;
    int pending_polls = 0;

    auto out = async::block_on(async::join(CountReadyInt { &ready_polls, 10 },
                                           CountPendingInt { &pending_polls, 20 }));

    EXPECT_EQ(out.get<0>(), 10);
    EXPECT_EQ(out.get<1>(), 20);
    EXPECT_EQ(ready_polls, 1);
    EXPECT_EQ(pending_polls, 2);
}

TEST(AsyncCoro, JoinSupportsVoidFuture) {
    auto out = async::block_on(async::join(async::yield_now(), ReadyInt {}));
    EXPECT_EQ(out.get<1>(), 7);
}

TEST(AsyncCoro, JoinAllPreservesOrder) {
    int polls[3] {};
    auto futures = Vec<IndexedPendingInt>::make();
    futures.push(IndexedPendingInt { polls, 0, 30 });
    futures.push(IndexedPendingInt { polls, 1, 10 });
    futures.push(IndexedPendingInt { polls, 2, 20 });

    auto out = async::block_on(async::join_all(rstd::move(futures)));

    ASSERT_EQ(out.len(), 3u);
    EXPECT_EQ(out[0], 30);
    EXPECT_EQ(out[1], 10);
    EXPECT_EQ(out[2], 20);
    EXPECT_EQ(polls[0], 2);
    EXPECT_EQ(polls[1], 2);
    EXPECT_EQ(polls[2], 2);
}

TEST(AsyncCoro, JoinAllHandlesEmptyAndVoid) {
    auto empty_futures = Vec<ReadyInt>::make();
    auto empty_out     = async::block_on(async::join_all(rstd::move(empty_futures)));
    EXPECT_TRUE(empty_out.is_empty());

    auto void_futures = Vec<async::YieldNow>::make();
    void_futures.push(async::yield_now());
    void_futures.push(async::yield_now());
    auto void_out = async::block_on(async::join_all(rstd::move(void_futures)));
    EXPECT_EQ(void_out.len(), 2u);
}

TEST(AsyncCoro, SleepWakesTask) {
    auto start = time::Instant::now();
    async::block_on(sleep_once());
    EXPECT_GE(start.elapsed().as_millis(), 5u);
}

TEST(AsyncCoro, PreludeExportsCoro) {
    EXPECT_EQ(async::block_on(prelude_coro_value()), 13);
}

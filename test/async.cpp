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

struct DropPendingInt {
    using Output = int;

    int* drops;

    explicit DropPendingInt(int* drops): drops(drops) {}
    DropPendingInt(const DropPendingInt&)            = delete;
    DropPendingInt& operator=(const DropPendingInt&) = delete;

    DropPendingInt(DropPendingInt&& other) noexcept
        : drops(rstd::exchange(other.drops, nullptr)) {}

    auto operator=(DropPendingInt&& other) noexcept -> DropPendingInt& {
        if (this != &other) {
            drops = rstd::exchange(other.drops, nullptr);
        }
        return *this;
    }

    ~DropPendingInt() {
        if (drops != nullptr) {
            ++*drops;
        }
    }

    auto poll(pin::Pin<mut_ref<DropPendingInt>>, task::Context&) -> task::Poll<int> {
        return task::Poll<int>::Pending();
    }
};

struct CounterStream {
    using Item = int;

    int next { 0 };

    auto poll_next(pin::Pin<mut_ref<CounterStream>> self, task::Context&)
        -> task::Poll<Option<int>> {
        auto& stream = *self.get_unchecked_mut();
        if (stream.next >= 2) {
            return task::Poll<Option<int>>::Ready(None());
        }
        return task::Poll<Option<int>>::Ready(Some(stream.next++));
    }
};

static_assert(future::StreamLike<CounterStream>);

struct MockAsyncIo {
    const u8*       read_data;
    usize           read_len;
    usize           read_pos { 0 };
    bytes::BytesMut written { bytes::BytesMut::make() };
    bool            read_pending { true };
    bool            write_pending { true };
    bool            flushed { false };
    bool            shutdown { false };

    auto poll_read(pin::Pin<mut_ref<MockAsyncIo>> self,
                   task::Context&                 cx,
                   bytes::BytesMut&               buf) -> task::Poll<io::Result<usize>> {
        auto& stream = *self.get_unchecked_mut();
        if (stream.read_pending) {
            stream.read_pending = false;
            cx.waker().wake_by_ref();
            return task::Poll<io::Result<usize>>::Pending();
        }

        auto remaining = stream.read_len - stream.read_pos;
        buf.extend_from_slice(stream.read_data + stream.read_pos, remaining);
        stream.read_pos = stream.read_len;
        return task::Poll<io::Result<usize>>::Ready(Ok(remaining));
    }

    auto poll_write(pin::Pin<mut_ref<MockAsyncIo>> self,
                    task::Context&                 cx,
                    const bytes::Bytes&            buf) -> task::Poll<io::Result<usize>> {
        auto& stream = *self.get_unchecked_mut();
        if (stream.write_pending) {
            stream.write_pending = false;
            cx.waker().wake_by_ref();
            return task::Poll<io::Result<usize>>::Pending();
        }

        stream.written.extend_from_slice(buf.data(), buf.len());
        return task::Poll<io::Result<usize>>::Ready(Ok(buf.len()));
    }

    auto poll_flush(pin::Pin<mut_ref<MockAsyncIo>> self, task::Context&)
        -> task::Poll<io::Result<empty>> {
        auto& stream  = *self.get_unchecked_mut();
        stream.flushed = true;
        return task::Poll<io::Result<empty>>::Ready(Ok(empty {}));
    }

    auto poll_shutdown(pin::Pin<mut_ref<MockAsyncIo>> self, task::Context&)
        -> task::Poll<io::Result<empty>> {
        auto& stream   = *self.get_unchecked_mut();
        stream.shutdown = true;
        return task::Poll<io::Result<empty>>::Ready(Ok(empty {}));
    }
};

static_assert(async::io::AsyncReadLike<MockAsyncIo>);
static_assert(async::io::AsyncWriteLike<MockAsyncIo>);

struct ErrorAsyncRead {
    auto poll_read(pin::Pin<mut_ref<ErrorAsyncRead>>,
                   task::Context&,
                   bytes::BytesMut&) -> task::Poll<io::Result<usize>> {
        return task::Poll<io::Result<usize>>::Ready(
            Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput })));
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

async::coro<bool> abort_join_child() {
    auto handle = async::spawn_local(child_value());
    handle.abort();
    auto result = co_await rstd::move(handle);
    if (result.is_ok()) {
        co_return false;
    }
    co_return result.unwrap_err().is_aborted();
}

async::coro<void> sleep_once() {
    co_await async::sleep(time::Duration::from_millis(10));
}

async::coro<int> oneshot_roundtrip() {
    auto channel = async::oneshot::channel<int>();
    auto tx      = rstd::move(channel.get<0>());
    auto rx      = rstd::move(channel.get<1>());
    auto sent    = tx.send(33);
    if (sent.is_err()) {
        co_return -1;
    }

    auto received = co_await rstd::move(rx);
    co_return received.unwrap();
}

async::coro<bool> oneshot_sender_dropped() {
    auto channel = async::oneshot::channel<int>();
    auto tx      = rstd::move(channel.get<0>());
    auto rx      = rstd::move(channel.get<1>());
    tx.close();

    auto received = co_await rstd::move(rx);
    co_return received.is_err();
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

TEST(AsyncCoro, JoinHandleAbortCompletesWithError) {
    EXPECT_TRUE(async::block_on(abort_join_child()));
}

TEST(AsyncCoro, OneShotRoundtrip) {
    EXPECT_EQ(async::block_on(oneshot_roundtrip()), 33);
}

TEST(AsyncCoro, OneShotSenderCloseWakesReceiver) {
    auto channel = async::oneshot::channel<int>();
    auto tx      = rstd::move(channel.get<0>());
    auto rx      = rstd::move(channel.get<1>());
    auto counts  = WakerCounts {};
    auto waker   = task::Waker::from_raw(task::RawWaker { &counts, &COUNT_WAKER_VTABLE });
    auto cx      = task::Context { waker };

    auto first = future::poll(rx, cx);
    EXPECT_TRUE(first.is_pending());
    EXPECT_EQ(counts.clones.load(), 1);

    tx.close();
    EXPECT_EQ(counts.wakes.load(), 1);

    auto second = future::poll(rx, cx);
    ASSERT_TRUE(second.is_ready());
    auto result = rstd::move(second).take();
    EXPECT_TRUE(result.is_err());
}

TEST(AsyncCoro, OneShotReceiverDropWakesSenderCancellation) {
    auto channel = async::oneshot::channel<int>();
    auto tx      = rstd::move(channel.get<0>());
    auto counts  = WakerCounts {};
    auto waker   = task::Waker::from_raw(task::RawWaker { &counts, &COUNT_WAKER_VTABLE });
    auto cx      = task::Context { waker };

    {
        auto rx    = rstd::move(channel.get<1>());
        auto first = tx.poll_canceled(future::pin_mut(tx), cx);
        EXPECT_TRUE(first.is_pending());
        EXPECT_EQ(counts.clones.load(), 1);
    }

    EXPECT_EQ(counts.wakes.load(), 1);

    auto second = tx.poll_canceled(future::pin_mut(tx), cx);
    ASSERT_TRUE(second.is_ready());
    rstd::move(second).take();
}

TEST(AsyncCoro, OneShotSendFailsAfterReceiverDrop) {
    auto channel = async::oneshot::channel<int>();
    auto tx      = rstd::move(channel.get<0>());
    {
        auto rx = rstd::move(channel.get<1>());
    }

    auto result = tx.send(55);
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err(), 55);
}

TEST(AsyncCoro, OneShotReportsSenderDropAsCanceled) {
    EXPECT_TRUE(async::block_on(oneshot_sender_dropped()));
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

TEST(AsyncCoro, SelectReturnsLeftAndDropsLoser) {
    int drops = 0;

    auto out = async::block_on(async::select(ReadyInt {}, DropPendingInt { &drops }));

    ASSERT_TRUE(out.is_left());
    EXPECT_EQ(rstd::move(out).unwrap_left(), 7);
    EXPECT_EQ(drops, 1);
}

TEST(AsyncCoro, SelectReturnsRightWhenLeftPending) {
    int left_polls = 0;

    auto out = async::block_on(
        async::select(CountPendingInt { &left_polls, 11 }, ReadyInt {}));

    ASSERT_TRUE(out.is_right());
    EXPECT_EQ(rstd::move(out).unwrap_right(), 7);
    EXPECT_EQ(left_polls, 1);
}

TEST(AsyncCoro, RaceReturnsWinnerOutput) {
    int left_polls = 0;

    auto out = async::block_on(
        async::race(CountPendingInt { &left_polls, 11 }, ReadyInt {}));

    EXPECT_EQ(out, 7);
    EXPECT_EQ(left_polls, 1);
}

TEST(AsyncCoro, TimeoutReadyFutureWins) {
    auto result = async::block_on(
        async::timeout(ReadyInt {}, time::Duration::from_millis(0)));

    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap(), 7);
}

TEST(AsyncCoro, TimeoutExpiresAndDropsFuture) {
    int drops = 0;
    auto result = async::block_on(
        async::timeout(DropPendingInt { &drops }, time::Duration::from_millis(1)));

    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(drops, 1);
}

TEST(AsyncCoro, StreamLikePollNext) {
    auto counts = WakerCounts {};
    auto waker  = task::Waker::from_raw(task::RawWaker { &counts, &COUNT_WAKER_VTABLE });
    auto cx     = task::Context { waker };
    auto stream = CounterStream {};

    auto first = future::poll_next(stream, cx);
    ASSERT_TRUE(first.is_ready());
    auto first_value = rstd::move(first).take();
    ASSERT_TRUE(first_value.is_some());
    EXPECT_EQ(rstd::move(first_value).unwrap_unchecked(), 0);

    auto second = future::poll_next(stream, cx);
    ASSERT_TRUE(second.is_ready());
    auto second_value = rstd::move(second).take();
    ASSERT_TRUE(second_value.is_some());
    EXPECT_EQ(rstd::move(second_value).unwrap_unchecked(), 1);

    auto done = future::poll_next(stream, cx);
    ASSERT_TRUE(done.is_ready());
    EXPECT_TRUE(rstd::move(done).take().is_none());
}

TEST(AsyncCoro, AsyncIoProtocolsUseBytesAndIoResult) {
    u8 read_data[] { 'a', 'b', 'c' };
    auto stream = MockAsyncIo { read_data, 3 };
    auto counts = WakerCounts {};
    auto waker  = task::Waker::from_raw(task::RawWaker { &counts, &COUNT_WAKER_VTABLE });
    auto cx     = task::Context { waker };
    auto read_buf = bytes::BytesMut::make();

    auto read_pending = async::io::poll_read(stream, cx, read_buf);
    EXPECT_TRUE(read_pending.is_pending());
    EXPECT_EQ(counts.wake_refs.load(), 1);

    auto read_ready = async::io::poll_read(stream, cx, read_buf);
    ASSERT_TRUE(read_ready.is_ready());
    auto read_result = rstd::move(read_ready).take();
    ASSERT_TRUE(read_result.is_ok());
    EXPECT_EQ(rstd::move(read_result).unwrap(), 3u);
    ASSERT_EQ(read_buf.len(), 3u);
    EXPECT_EQ(read_buf[0], 'a');
    EXPECT_EQ(read_buf[2], 'c');

    u8 write_data[] { 'x', 'y' };
    auto write_buf = bytes::Bytes::copy_from_slice(slice<u8>::from_raw_parts(write_data, 2));
    auto write_pending = async::io::poll_write(stream, cx, write_buf);
    EXPECT_TRUE(write_pending.is_pending());
    EXPECT_EQ(counts.wake_refs.load(), 2);

    auto write_ready = async::io::poll_write(stream, cx, write_buf);
    ASSERT_TRUE(write_ready.is_ready());
    auto write_result = rstd::move(write_ready).take();
    ASSERT_TRUE(write_result.is_ok());
    EXPECT_EQ(rstd::move(write_result).unwrap(), 2u);
    ASSERT_EQ(stream.written.len(), 2u);
    EXPECT_EQ(stream.written[0], 'x');
    EXPECT_EQ(stream.written[1], 'y');

    auto flush = async::io::poll_flush(stream, cx);
    ASSERT_TRUE(flush.is_ready());
    ASSERT_TRUE(rstd::move(flush).take().is_ok());
    EXPECT_TRUE(stream.flushed);

    auto shutdown = async::io::poll_shutdown(stream, cx);
    ASSERT_TRUE(shutdown.is_ready());
    ASSERT_TRUE(rstd::move(shutdown).take().is_ok());
    EXPECT_TRUE(stream.shutdown);
}

TEST(AsyncCoro, AsyncIoReadErrorUsesIoResult) {
    auto counts = WakerCounts {};
    auto waker  = task::Waker::from_raw(task::RawWaker { &counts, &COUNT_WAKER_VTABLE });
    auto cx     = task::Context { waker };
    auto reader = ErrorAsyncRead {};
    auto buf    = bytes::BytesMut::make();

    auto out = async::io::poll_read(reader, cx, buf);
    ASSERT_TRUE(out.is_ready());
    auto result = rstd::move(out).take();
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err().kind(),
              io::error::ErrorKind { io::error::ErrorKind::InvalidInput });
}

TEST(AsyncCoro, SleepWakesTask) {
    auto start = time::Instant::now();
    async::block_on(sleep_once());
    EXPECT_GE(start.elapsed().as_millis(), 5u);
}

TEST(AsyncCoro, PreludeExportsCoro) {
    EXPECT_EQ(async::block_on(prelude_coro_value()), 13);
}

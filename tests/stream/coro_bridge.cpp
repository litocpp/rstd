#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd::prelude;
using namespace rstd::literals;
namespace async  = rstd::async;
namespace stream = rstd::stream;
namespace future = rstd::future;
using rstd::task::Context;
using rstd::time::Duration;

auto bridge_ready_value() -> coro<int> {
    co_return 7;
}
TEST(CoroFuture, IntoFutureTraitConsumesCoroutineAndExistingFuturePassesThrough) {
    auto value   = bridge_ready_value();
    auto adapted = as<IntoFuture>(value).into_future();
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    EXPECT_EQ(runtime.block_on(rstd::move(adapted)), 7);
    auto yielded = async::into_future(async::yield_now());
    runtime.block_on(rstd::move(yielded));
}

struct BridgeDrop {
    int* count;
    explicit BridgeDrop(int& value): count(&value) {}
    BridgeDrop(const BridgeDrop&) = delete;
    BridgeDrop(BridgeDrop&& other): count(rstd::exchange(other.count, nullptr)) {}
    ~BridgeDrop() {
        if (count) ++*count;
    }
};
auto delayed_value(int value, int& started, BridgeDrop drop) -> coro<int> {
    ++started;
    co_await async::sleep(Duration::from_millis(u64(15)));
    co_return value;
}
auto nested_value(int& started, int& drops) -> coro<int> {
    co_return co_await delayed_value(31, started, BridgeDrop(drops));
}
auto retry_coro() -> coro<empty> {
    int  started = 0, drops = 0;
    auto source = stream::then(stream::once(1), [&](int) {
        return async::into_future(nested_value(started, drops));
    });
    EXPECT_TRUE((co_await async::timeout(stream::next(source), Duration {})).is_err());
    EXPECT_EQ(started, 1);
    EXPECT_EQ(drops, 0);
    auto result = co_await async::timeout(stream::next(source), Duration::from_secs(u64(2)));
    EXPECT_TRUE(result.is_ok());
    if (result.is_ok()) EXPECT_EQ(rstd::move(result).unwrap().unwrap(), 31);
    EXPECT_EQ(started, 1);
    EXPECT_EQ(drops, 1);
    co_return empty {};
}
TEST(CoroFuture, NestedNativeSleepSurvivesNextTimeout) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    runtime.block_on(retry_coro());
    int started = 0, drops = 0;
    EXPECT_EQ(runtime.block_on(nested_value(started, drops)), 31);
    EXPECT_EQ(drops, 1);
}

auto suspend_forever(BridgeDrop drop) -> coro<void> {
    auto notification = async::Notify::make().unwrap();
    co_await notification.notified();
}
auto cancel_coro() -> coro<empty> {
    int drops = 0;
    {
        auto adapted = async::into_future(suspend_forever(BridgeDrop(drops)));
        EXPECT_TRUE((co_await async::timeout(future::by_ref(adapted), Duration {})).is_err());
        auto moved = rstd::move(adapted);
        EXPECT_EQ(drops, 0);
    }
    EXPECT_EQ(drops, 1);
    co_return empty {};
}
TEST(CoroFuture, MovePreservesFrameAndDropReleasesPendingOperation) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    runtime.block_on(cancel_coro());
}

auto next_number(int state) -> coro<Option<rstd::tuple<int, int>>> {
    co_await async::sleep(Duration::from_millis(u64(1)));
    if (state == 3) co_return None();
    co_return Some(rstd::tuple<int, int> { state * 2, state + 1 });
}
TEST(CoroFuture, UnfoldOwnsStateAcrossNativeAwaits) {
    auto source  = stream::unfold(0, [](int state) {
        return async::into_future(next_number(state));
    });
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto values  = runtime.block_on(stream::collect<Vec<int>>(rstd::move(source)));
    ASSERT_TRUE(values.len() == usize(3));
    EXPECT_EQ(values[usize(0)], 0);
    EXPECT_EQ(values[usize(1)], 2);
    EXPECT_EQ(values[usize(2)], 4);
}

auto emit_bytes(rstd::net::TcpListener& listener) -> coro<empty> {
    auto connection = (co_await listener.accept()).unwrap();
    auto socket     = rstd::move(connection.get<0>());
    co_await async::sleep(Duration::from_millis(u64(15)));
    auto bytes = rstd::bytes::Bytes::copy_from_slice("data"_str.as_bytes());
    EXPECT_TRUE((co_await async::io::write_all(socket, bytes)).is_ok());
    co_return empty {};
}
auto read_operation(rstd::net::SocketAddr address) -> coro<usize> {
    auto socket   = (co_await rstd::net::TcpStream::connect(address)).unwrap();
    auto owned    = (co_await rstd::move(socket).into_owned_socket()).unwrap();
    auto source   = async::CompletionSource::socket(owned.as_socket());
    auto result   = co_await async::IoOperation::read(source, usize(4));
    auto released = co_await rstd::move(source).release();
    EXPECT_TRUE(released.is_ok());
    EXPECT_TRUE(result.is_ok());
    if (result.is_err()) co_return usize();
    EXPECT_TRUE(result->data() == "data"_str.as_bytes());
    co_return result->transferred();
}
auto io_roundtrip(async::RuntimeHandle    runtime,
                  rstd::net::TcpListener& listener,
                  rstd::net::SocketAddr   address) -> coro<empty> {
    auto peer   = async::AbortOnDropHandle(runtime.spawn(emit_bytes(listener)));
    auto source = stream::then(stream::once(address), [](auto address) {
        return async::into_future(read_operation(address));
    });
    EXPECT_TRUE((co_await async::timeout(stream::next(source), Duration {})).is_err());
    auto value = co_await async::timeout(stream::next(source), Duration::from_secs(u64(2)));
    EXPECT_TRUE(value.is_ok());
    if (value.is_ok()) EXPECT_TRUE(rstd::move(value).unwrap().unwrap() == usize(4));
    EXPECT_TRUE((co_await rstd::move(peer)).is_ok());
    co_return empty {};
}
TEST(CoroFuture, NativeIoAndSourceReleaseUseExistingPollOwner) {
    auto listener =
        rstd::net::TcpListener::bind(rstd::net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto address = listener.local_addr().unwrap();
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    runtime.block_on(io_roundtrip(runtime.handle(), listener, address));
}

auto executor_segment(async::LocalExecutor executor, int& runs, BridgeDrop drop) -> coro<bool> {
    auto accepted = co_await executor;
    if (accepted) ++runs;
    co_return accepted;
}
TEST(CoroFuture, ExecutorHandoffUsesExistingJobWithoutTask) {
    auto executor = async::LocalExecutorContext::make();
    int  runs = 0, drops = 0;
    auto adapted =
        async::into_future(executor_segment(executor.executor(), runs, BridgeDrop(drops)));
    auto cx = Context(rstd::task::Waker::noop());
    EXPECT_TRUE(future::poll(adapted, cx).is_pending());
    EXPECT_EQ(executor.run_ready().to_primitive(), 1);
    auto result = future::poll(adapted, cx);
    ASSERT_TRUE(result.is_ready());
    EXPECT_TRUE(rstd::move(result).take());
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(drops, 1);
}
TEST(CoroFuture, DroppedQueuedHandoffCannotAccessReleasedFrame) {
    auto executor = async::LocalExecutorContext::make();
    int  runs = 0, drops = 0;
    {
        auto adapted =
            async::into_future(executor_segment(executor.executor(), runs, BridgeDrop(drops)));
        auto cx = Context(rstd::task::Waker::noop());
        EXPECT_TRUE(future::poll(adapted, cx).is_pending());
    }
    EXPECT_EQ(drops, 1);
    EXPECT_EQ(executor.run_ready().to_primitive(), 1);
    EXPECT_EQ(runs, 0);
}
TEST(CoroFuture, CanceledExecutorJobReturnsFalseOnOwner) {
    int                             runs = 0, drops = 0;
    Option<async::CoroFuture<bool>> adapted;
    {
        auto executor = async::LocalExecutorContext::make();
        adapted.insert(
            async::into_future(executor_segment(executor.executor(), runs, BridgeDrop(drops))));
        auto cx = Context(rstd::task::Waker::noop());
        EXPECT_TRUE(future::poll(*adapted, cx).is_pending());
    }
    auto cx     = Context(rstd::task::Waker::noop());
    auto result = future::poll(*adapted, cx);
    ASSERT_TRUE(result.is_ready());
    EXPECT_FALSE(rstd::move(result).take());
    EXPECT_EQ(runs, 0);
    EXPECT_EQ(drops, 1);
}

TEST(CoroFuture, NativeExecutorAwaitRetainsTaskHandoffBehavior) {
    auto  executor = async::LocalExecutorContext::make();
    auto  runtime  = async::RuntimeBuilder::multi_thread()
                         .worker_threads(usize(1))
                         .enable_all()
                         .build()
                         .unwrap();
    int   runs = 0, drops = 0;
    auto  joined = runtime.spawn(executor_segment(executor.executor(), runs, BridgeDrop(drops)));
    usize jobs {};
    for (int attempts = 0; attempts != 1000 && ! joined.is_finished(); ++attempts) {
        jobs += executor.run_ready();
        if (! joined.is_finished()) rstd::thread::sleep(Duration::from_millis(u64(1)));
    }
    EXPECT_TRUE(joined.is_finished());
    if (! joined.is_finished()) joined.abort();
    auto result = runtime.block_on(rstd::move(joined));
    EXPECT_TRUE(result.is_ok());
    if (result.is_ok()) EXPECT_TRUE(*result);
    EXPECT_TRUE(jobs == usize(1));
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(drops, 1);
}

struct ExternalGate {
    rstd::sync::Mutex<bool>          released { false };
    rstd::sync::Condvar              changed;
    rstd::sync::atomic::Atomic<bool> entered { false };
    rstd::sync::atomic::Atomic<bool> finished { false };
    void                             release() {
        {
            auto guard = released.lock().unwrap();
            *guard     = true;
        }
        changed.notify_all();
    }
};
auto external_wait(async::AnyExecutor executor, ExternalGate& gate, BridgeDrop drop) -> coro<void> {
    if (! co_await executor) co_return;
    gate.entered.store(true);
    auto guard = gate.released.lock().unwrap();
    gate.changed.wait_while(guard, [](bool value) {
        return ! value;
    });
    gate.finished.store(true);
}
TEST(CoroFuture, PollDoesNotBlockExecutingSegmentAndDropProtectsBorrowedInputs) {
    auto pool = rstd::thread::ThreadPoolBuilder::make().worker_count(usize(1)).build().unwrap();
    auto executor =
        async::AnyExecutor::from_executor(async::ThreadPoolExecutor::from_handle(pool.handle()));
    ExternalGate                            gate;
    int                                     drops = 0;
    Option<rstd::thread::JoinHandle<empty>> releaser;
    {
        auto adapted =
            async::into_future(external_wait(rstd::move(executor), gate, BridgeDrop(drops)));
        auto cx = Context(rstd::task::Waker::noop());
        EXPECT_TRUE(future::poll(adapted, cx).is_pending());
        while (! gate.entered.load()) rstd::hint::spin_loop();
        EXPECT_TRUE(future::poll(adapted, cx).is_pending());
        EXPECT_FALSE(gate.finished.load());
        releaser.insert(rstd::thread::spawn([&gate] {
                            gate.release();
                            return empty {};
                        }).unwrap());
    }
    EXPECT_TRUE(gate.finished.load());
    EXPECT_EQ(drops, 1);
    (void)releaser.take().unwrap().join().unwrap();
}

TEST(CoroFuture, MutexTryLockReturnsWouldBlockAndAdoptedGuardUnlocks) {
    rstd::sync::Mutex<int> value { 3 };
    {
        auto held    = value.lock().unwrap();
        auto blocked = value.try_lock();
        EXPECT_TRUE(blocked.is_err());
        EXPECT_TRUE(blocked.unwrap_err() == rstd::sync::TryLockError::WouldBlock);
    }
    {
        auto held = value.try_lock().unwrap();
        *held     = 4;
    }
    EXPECT_EQ(*value.lock().unwrap(), 4);
}

auto pending_for_shutdown(rstd::sync::atomic::Atomic<bool>& entered, BridgeDrop drop)
    -> coro<void> {
    auto notification = async::Notify::make().unwrap();
    entered.store(true);
    co_await notification.notified();
}
TEST(CoroFuture, RuntimeShutdownReleasesAdaptedPendingFrameBeforeJoinCompletes) {
    rstd::sync::atomic::Atomic<bool> entered { false };
    int                              drops  = 0;
    auto                             joined = [&] {
        auto runtime = async::RuntimeBuilder::multi_thread()
                           .worker_threads(usize(1))
                           .enable_all()
                           .build()
                           .unwrap();
        auto joined =
            runtime.spawn(async::into_future(pending_for_shutdown(entered, BridgeDrop(drops))));
        while (! entered.load()) rstd::hint::spin_loop();
        return joined;
    }();
    EXPECT_EQ(drops, 1);
    EXPECT_TRUE(joined.is_finished());
    auto result = async::block_on(rstd::move(joined));
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(drops, 1);
}

#include <rstd/macro.hpp>
#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;

using namespace rstd::prelude;
using namespace rstd;

#if RSTD_OS_LINUX || RSTD_OS_MACOS || RSTD_OS_WINDOWS
auto notify_cancel_and_rearm() -> async::coro<bool> {
    auto notify   = async::Notify::make().unwrap();
    auto notifier = notify.notifier();
    for (int index = 0; index < 131072; ++index)
        if (notifier.notify().is_err()) co_return false;
    auto first = co_await async::timeout(notify.notified(), time::Duration::from_secs(u64(2)));
    if (first.is_err() || first->is_err()) co_return false;
    auto canceled = co_await async::timeout(notify.notified(), time::Duration::from_millis(u64(5)));
    if (canceled.is_ok()) co_return false;
    if (notifier.notify().is_err()) co_return false;
    auto next = co_await async::timeout(notify.notified(), time::Duration::from_secs(u64(2)));
    co_return next.is_ok() && next->is_ok();
}

auto external_notify(async::Runtime& runtime) -> bool {
    auto pending = [] {
        auto owner = async::Notify::make().unwrap();
        return tuple<async::NotifyFuture, async::NotifyHandle> { owner.notified(),
                                                                 owner.notifier() };
    }();
    auto notifier = rstd::move(pending.template get<1>());
    auto sender   = thread::spawn([notifier = notifier.clone()] {
                      thread::sleep(time::Duration::from_millis(u64(5)));
                      return notifier.notify();
                    }).unwrap();
    auto result   = runtime.block_on(
        async::timeout(rstd::move(pending.template get<0>()), time::Duration::from_secs(u64(2))));
    auto sent = rstd::move(sender).join().unwrap();
    return result.is_ok() && result->is_ok() && sent.is_ok();
}

TEST(AsyncNotify, CoalescesNotificationsAndCanRearmAfterCanceledWait) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    EXPECT_TRUE(runtime.block_on(notify_cancel_and_rearm()));
}

TEST(AsyncNotify, WakesFromExternalThreadAfterOriginalOwnerDrops) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    EXPECT_TRUE(external_notify(runtime));
}

TEST(AsyncNotify, WakesAcrossRuntimeWorkers) {
    auto runtime = async::RuntimeBuilder::multi_thread()
                       .worker_threads(usize(2))
                       .enable_all()
                       .build()
                       .unwrap();
    EXPECT_TRUE(external_notify(runtime));
}

TEST(AsyncNotify, TransfersCanceledGrantAndRemovesQueuedWaiters) {
    auto notify = async::Notify::make().unwrap();
    auto sender = notify.notifier();
    auto cx     = task::Context { task::Waker::noop() };
    auto first  = Some(notify.notified());
    auto middle = Some(notify.notified());
    auto last   = notify.notified();
    EXPECT_TRUE(future::poll(*first, cx).is_pending());
    EXPECT_TRUE(future::poll(*middle, cx).is_pending());
    EXPECT_TRUE(future::poll(last, cx).is_pending());
    middle = None();
    EXPECT_TRUE(sender.notify().is_ok());
    first = None();
    EXPECT_TRUE(future::poll(last, cx).is_ready());
    auto next = notify.notified();
    EXPECT_TRUE(future::poll(next, cx).is_pending());
    EXPECT_TRUE(sender.notify().is_ok());
    EXPECT_TRUE(future::poll(next, cx).is_ready());
}

TEST(AsyncNotify, MovesKeepWaiterIdentityAndReturnUnusedPermit) {
    auto notify = async::Notify::make().unwrap();
    auto sender = notify.notifier();
    auto cx     = task::Context { task::Waker::noop() };
    auto first  = notify.notified();
    auto second = notify.notified();
    EXPECT_TRUE(future::poll(first, cx).is_pending());
    EXPECT_TRUE(future::poll(second, cx).is_pending());
    first      = rstd::move(second);
    auto moved = Some(rstd::move(first));
    EXPECT_TRUE(sender.notify().is_ok());
    moved     = None();
    auto next = notify.notified();
    EXPECT_TRUE(future::poll(next, cx).is_ready());
    auto pending = notify.notified();
    EXPECT_TRUE(future::poll(pending, cx).is_pending());
}

TEST(AsyncNotify, CancelRacesWithCrossThreadNotification) {
    for (int iteration = 0; iteration < 100; ++iteration) {
        auto notify   = async::Notify::make().unwrap();
        auto canceled = Some(notify.notified());
        auto next     = notify.notified();
        auto cx       = task::Context { task::Waker::noop() };
        EXPECT_TRUE(future::poll(*canceled, cx).is_pending());
        EXPECT_TRUE(future::poll(next, cx).is_pending());
        auto sender = thread::spawn([handle = notify.notifier()] {
                          return handle.notify();
                      }).unwrap();
        canceled    = None();
        EXPECT_TRUE(rstd::move(sender).join().unwrap().is_ok());
        EXPECT_TRUE(future::poll(next, cx).is_ready());
    }
}

auto shutdown_waiters() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto handle     = supervisor.handle();
    Option<async::JoinHandle<io::Result<empty>>> first, second;
    {
        auto reservation = handle.prepare().unwrap();
        for (int attempt = 0; attempt < 2; ++attempt) {
            auto result = co_await async::timeout(supervisor.shutdown().into_future(),
                                                  time::Duration::from_millis(u64(5)));
            if (result.is_ok()) co_return false;
        }
        first  = Some(async::spawn(supervisor.shutdown()));
        second = Some(async::spawn(supervisor.shutdown()));
        co_await async::sleep(time::Duration::from_millis(u64(5)));
        if (first->is_finished() || second->is_finished() || handle.accepting()) co_return false;
    }
    auto a      = co_await first.take().unwrap();
    auto b      = co_await second.take().unwrap();
    auto repeat = co_await supervisor.shutdown();
    co_return a.is_ok() && a->is_ok() && b.is_ok() && b->is_ok() && repeat.is_ok();
}

auto shutdown_after_move() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make().unwrap();
    auto pending    = supervisor.shutdown();
    auto moved      = rstd::move(supervisor);
    auto first      = co_await rstd::move(pending);
    auto second     = co_await moved.shutdown();
    co_return first.is_ok() && second.is_ok();
}

auto shutdown_after_drop() -> async::coro<bool> {
    Option<async::coro<io::Result<empty>>> pending;
    {
        auto supervisor = async::ProcessSupervisor::make().unwrap();
        pending         = Some(supervisor.shutdown());
    }
    co_return (co_await pending.take().unwrap()).is_ok();
}

auto reject_without_io() -> async::coro<bool> {
    auto supervisor = async::ProcessSupervisor::make();
    if (supervisor.is_ok()) co_return false;
    auto rejected = supervisor.unwrap_err().kind() == io::ErrorKind { io::ErrorKind::Unsupported };
    co_await async::sleep(time::Duration::from_millis(u64(1)));
    co_return rejected;
}

auto begin_supervision(Option<async::ProcessSupervisor>& owner,
                       Option<async::SpawnReservation>&  reservation) -> async::coro<void> {
    owner       = Some(async::ProcessSupervisor::make().unwrap());
    reservation = Some(owner->handle().prepare().unwrap());
    co_await async::yield_now();
}

TEST(AsyncProcess, ShutdownCanBeCanceledRepeatedAndAwaitedConcurrently) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    EXPECT_TRUE(runtime.block_on(shutdown_waiters()));
}

TEST(AsyncProcess, ShutdownWaitersWorkAcrossWorkers) {
    auto runtime = async::RuntimeBuilder::multi_thread()
                       .worker_threads(usize(2))
                       .enable_all()
                       .build()
                       .unwrap();
    EXPECT_TRUE(runtime.block_on(shutdown_waiters()));
}

TEST(AsyncProcess, ShutdownOwnsStateAcrossMoveAndDrop) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    EXPECT_TRUE(runtime.block_on(shutdown_after_move()));
    EXPECT_TRUE(runtime.block_on(shutdown_after_drop()));
}

TEST(AsyncProcess, MissingRuntimeAndIoAreErrors) {
    auto outside = async::ProcessSupervisor::make();
    ASSERT_TRUE(outside.is_err());
    EXPECT_EQ(outside.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::NotConnected });
    auto runtime = async::RuntimeBuilder::current_thread().enable_time().build().unwrap();
    EXPECT_TRUE(runtime.block_on(reject_without_io()));
}

TEST(AsyncProcess, RuntimeAbortPublishesRepeatableShutdownError) {
    Option<async::ProcessSupervisor> owner;
    Option<async::SpawnReservation>  reservation;
    {
        auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
        runtime.block_on(begin_supervision(owner, reservation));
    }
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    for (int attempt = 0; attempt < 2; ++attempt) {
        auto result = runtime.block_on(owner->shutdown());
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::Interrupted });
    }
    EXPECT_FALSE(owner->handle().accepting());
    reservation = None();
}
#endif

int main() {
    return rstd::test::run_registered().to_primitive();
}

#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd::prelude;
using rstd::async::Mutex;
using rstd::async::MutexGuard;
using rstd::async::MutexLock;
using rstd::async::coro;
using rstd::async::RuntimeBuilder;
using rstd::future::poll;
using rstd::task::Context;
using rstd::task::Waker;

TEST(AsyncMutex, FifoGrantCannotBeStolen) {
    auto mutex  = Mutex<int>(1);
    auto cx     = Context { Waker::noop() };
    auto owner  = Some(rstd::async::block_on(mutex.lock()));
    auto first  = mutex.lock();
    auto second = mutex.lock();
    EXPECT_TRUE(poll(first, cx).is_pending());
    EXPECT_TRUE(poll(second, cx).is_pending());
    owner      = None();
    auto third = mutex.lock();
    EXPECT_TRUE(poll(third, cx).is_pending());
    EXPECT_TRUE(poll(second, cx).is_pending());
    owner   = Some(poll(first, cx).take());
    **owner = 7;
    owner   = None();
    owner   = Some(poll(second, cx).take());
    EXPECT_EQ(**owner, 7);
    owner = None();
    EXPECT_TRUE(poll(third, cx).is_ready());
}

TEST(AsyncMutex, CancelQueuedAndGrantedWaiters) {
    auto mutex  = Mutex<int>(1);
    auto cx     = Context { Waker::noop() };
    auto owner  = Some(rstd::async::block_on(mutex.lock()));
    auto first  = Some(mutex.lock());
    auto middle = Some(mutex.lock());
    auto last   = Some(mutex.lock());
    auto next   = mutex.lock();
    EXPECT_TRUE(poll(*first, cx).is_pending());
    EXPECT_TRUE(poll(*middle, cx).is_pending());
    EXPECT_TRUE(poll(*last, cx).is_pending());
    middle = None();
    last   = None();
    EXPECT_TRUE(poll(next, cx).is_pending());
    owner = None();
    first = None();
    EXPECT_TRUE(poll(next, cx).is_ready());
    auto unlocked = mutex.lock();
    EXPECT_TRUE(poll(unlocked, cx).is_ready());
}

TEST(AsyncMutex, MovesPreserveQueueAndCancelReplacedWaiter) {
    auto mutex  = Mutex<int>(1);
    auto cx     = Context { Waker::noop() };
    auto owner  = Some(rstd::async::block_on(mutex.lock()));
    auto first  = mutex.lock();
    auto second = mutex.lock();
    EXPECT_TRUE(poll(first, cx).is_pending());
    EXPECT_TRUE(poll(second, cx).is_pending());
    first      = rstd::move(second);
    auto moved = rstd::move(first);
    owner      = None();
    EXPECT_TRUE(poll(moved, cx).is_ready());
}

TEST(AsyncMutex, GuardAndFutureOwnMutexLifetime) {
    auto pending = [] {
        auto mutex = Mutex<int>(42);
        return mutex.lock();
    }();
    auto guard = rstd::async::block_on(rstd::move(pending));
    EXPECT_EQ(*guard, 42);
    auto other         = Mutex<int>(10);
    auto other_guard   = rstd::async::block_on(other.lock());
    auto pending_other = other.lock();
    auto cx            = Context { Waker::noop() };
    EXPECT_TRUE(poll(pending_other, cx).is_pending());
    other_guard = rstd::move(guard);
    EXPECT_EQ(*other_guard, 42);
    EXPECT_TRUE(poll(pending_other, cx).is_ready());
}

auto increment(Mutex<int> mutex) -> coro<void> {
    for (int i = 0; i < 100; ++i) {
        auto guard  = co_await mutex.lock();
        auto before = *guard;
        co_await rstd::async::yield_now();
        *guard = before + 1;
    }
}
TEST(AsyncMutex, MutualExclusionAcrossSuspensionAndWorkers) {
    auto mutex   = Mutex<int>(0);
    auto runtime = RuntimeBuilder::multi_thread().worker_threads(usize(4)).build().unwrap();
    auto a       = runtime.spawn(increment(mutex.clone()));
    auto b       = runtime.spawn(increment(mutex.clone()));
    auto c       = runtime.spawn(increment(mutex.clone()));
    auto d       = runtime.spawn(increment(mutex.clone()));
    EXPECT_TRUE(runtime.block_on(rstd::move(a)).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(b)).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(c)).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(d)).is_ok());
    EXPECT_EQ(*runtime.block_on(mutex.lock()), 400);
}

TEST(AsyncMutex, CancelRacesWithCrossThreadGrant) {
    for (int iteration = 0; iteration < 100; ++iteration) {
        auto mutex    = Mutex<int>(42);
        auto guard    = rstd::async::block_on(mutex.lock());
        auto canceled = Some(mutex.lock());
        auto next     = mutex.lock();
        auto cx       = Context { Waker::noop() };
        EXPECT_TRUE(poll(*canceled, cx).is_pending());
        EXPECT_TRUE(poll(next, cx).is_pending());
        auto releaser = rstd::thread::spawn([guard = rstd::move(guard)]() mutable {
                            auto owned = rstd::move(guard);
                            return *owned;
                        }).unwrap();
        canceled      = None();
        EXPECT_EQ(*rstd::async::block_on(rstd::move(next)), 42);
        EXPECT_EQ(rstd::move(releaser).join().unwrap(), 42);
    }
}

struct WakeCounter {
    int         wakes {};
    int         clones {};
    static auto clone(void* data) -> rstd::task::RawWaker;
    static void wake(void* data) { ++static_cast<WakeCounter*>(data)->wakes; }
    static void drop(void*) {}
};
const rstd::task::RawWakerVTable counter_vtable { &WakeCounter::clone,
                                                  &WakeCounter::wake,
                                                  &WakeCounter::wake,
                                                  &WakeCounter::drop };
auto                             WakeCounter::clone(void* data) -> rstd::task::RawWaker {
    ++static_cast<WakeCounter*>(data)->clones;
    return rstd::task::RawWaker::from_raw_parts(data, &counter_vtable);
}
TEST(AsyncMutex, RepollReplacesWakerWithoutDuplicatingWaiter) {
    auto mutex           = Mutex<int>(0);
    auto guard           = Some(rstd::async::block_on(mutex.lock()));
    auto waiter          = mutex.lock();
    auto old             = WakeCounter {};
    auto current         = WakeCounter {};
    auto old_waker       = Waker::from_raw_parts(&old, &counter_vtable);
    auto current_waker   = Waker::from_raw_parts(&current, &counter_vtable);
    auto old_context     = Context { old_waker };
    auto current_context = Context { current_waker };
    EXPECT_TRUE(poll(waiter, old_context).is_pending());
    EXPECT_TRUE(poll(waiter, old_context).is_pending());
    EXPECT_EQ(old.clones, 1);
    EXPECT_TRUE(poll(waiter, current_context).is_pending());
    guard = None();
    EXPECT_EQ(old.wakes, 0);
    EXPECT_EQ(current.wakes, 1);
    EXPECT_TRUE(poll(waiter, current_context).is_ready());
    EXPECT_EQ(current.wakes, 1);
}

struct UnlockOnWakerDrop {
    Option<MutexGuard<int>>* owner;
    bool                     armed {};
    static auto              clone(void* data) -> rstd::task::RawWaker;
    static void              wake(void*) {}
    static void              drop(void* data) {
        auto& state = *static_cast<UnlockOnWakerDrop*>(data);
        if (rstd::exchange(state.armed, false)) *state.owner = None();
    }
};
const rstd::task::RawWakerVTable unlock_vtable { &UnlockOnWakerDrop::clone,
                                                 &UnlockOnWakerDrop::wake,
                                                 &UnlockOnWakerDrop::wake,
                                                 &UnlockOnWakerDrop::drop };
auto                             UnlockOnWakerDrop::clone(void* data) -> rstd::task::RawWaker {
    return rstd::task::RawWaker::from_raw_parts(data, &unlock_vtable);
}

TEST(AsyncMutex, ReplacedWakerCanReleaseGuardWithoutDeadlock) {
    auto mutex  = Mutex<int>(42);
    auto owner  = Some(rstd::async::block_on(mutex.lock()));
    auto waiter = mutex.lock();
    auto state  = UnlockOnWakerDrop { &owner };
    auto old    = Waker::from_raw_parts(&state, &unlock_vtable);
    auto old_cx = Context { old };
    auto new_cx = Context { Waker::noop() };
    EXPECT_TRUE(poll(waiter, old_cx).is_pending());
    state.armed = true;
    EXPECT_TRUE(poll(waiter, new_cx).is_pending());
    EXPECT_TRUE(owner.is_none());
    auto result = poll(waiter, new_cx);
    ASSERT_TRUE(result.is_ready());
    EXPECT_EQ(*rstd::move(result).take(), 42);
}

int main() {
    return rstd::test::run_registered().to_primitive();
}

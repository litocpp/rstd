#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::async::RuntimeBuilder;
using rstd::async::RuntimeHandle;
using rstd::time::Duration;
using rstd::future::by_ref;

auto value_task() -> coro<String> {
    co_return String::make("owned value"_str);
}

TEST(JoinBorrow, ConsumesResultThroughBorrow) {
    auto rt  = RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto job = rt.spawn(value_task());
    EXPECT_TRUE(rt.block_on(by_ref(job)).unwrap() == "owned value"_str);
    EXPECT_TRUE(job.is_finished());
}

TEST(JoinBorrow, DroppingBorrowDoesNotAbortTask) {
    auto rt  = RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto job = rt.spawn(value_task());
    {
        auto borrowed = by_ref(job);
        (void)borrowed;
    }
    EXPECT_TRUE(rt.block_on(rstd::move(job)).unwrap() == "owned value"_str);
}

struct Pending {
    using Output = empty;
    auto poll(mut_ref<Pending>, rstd::task::Context&) -> rstd::task::Poll<Output> {
        return rstd::task::Poll<Output>::Pending();
    }
};
struct DropFlag {
    bool* destroyed;
    ~DropFlag() { *destroyed = true; }
};
auto pending_task(bool& destroyed) -> coro<empty> {
    auto guard = DropFlag { &destroyed };
    co_await Pending {};
    co_return empty {};
}
auto abort_and_join(RuntimeHandle runtime) -> coro<empty> {
    bool destroyed = false;
    auto job       = runtime.spawn(pending_task(destroyed));
    auto joined    = co_await rstd::async::timeout(by_ref(job), Duration::from_millis(u64(10)));
    EXPECT_TRUE(joined.is_err());
    EXPECT_FALSE(job.is_finished());
    job.abort();
    auto result = co_await by_ref(job);
    EXPECT_TRUE(destroyed);
    EXPECT_TRUE(result.is_err());
    co_return empty {};
}
TEST(JoinBorrow, TimeoutRetainsHandleAndAbortReleasesFrameBeforeJoin) {
    auto rt = RuntimeBuilder::current_thread().enable_all().build().unwrap();
    rt.block_on(abort_and_join(rt.handle()));
}

auto select_join(RuntimeHandle runtime) -> coro<empty> {
    bool destroyed = false;
    auto pending   = runtime.spawn(pending_task(destroyed));
    auto value     = runtime.spawn(value_task());
    auto selected  = co_await rstd::async::select(by_ref(pending), by_ref(value));
    EXPECT_TRUE(selected.is_right());
    EXPECT_TRUE(rstd::move(selected).unwrap_right().unwrap() == "owned value"_str);
    pending.abort();
    EXPECT_TRUE((co_await rstd::move(pending)).is_err());
    EXPECT_TRUE(destroyed);
    co_return empty {};
}
TEST(JoinBorrow, SelectRetainsLoserForAbortAndJoin) {
    auto rt = RuntimeBuilder::current_thread().enable_all().build().unwrap();
    rt.block_on(select_join(rt.handle()));
}

struct ObserveOnWakerDrop {
    rstd::async::JoinHandle<String>* handle;
    bool                             armed {};
    bool                             observed {};
    int                              clones {};
    static auto                      clone(void* data) -> rstd::task::RawWaker;
    static void                      wake(void*) {}
    static void                      drop(void* data) {
        auto& state = *static_cast<ObserveOnWakerDrop*>(data);
        if (rstd::exchange(state.armed, false)) state.observed = ! state.handle->is_finished();
    }
};
const rstd::task::RawWakerVTable observer_vtable { &ObserveOnWakerDrop::clone,
                                                   &ObserveOnWakerDrop::wake,
                                                   &ObserveOnWakerDrop::wake,
                                                   &ObserveOnWakerDrop::drop };
auto                             ObserveOnWakerDrop::clone(void* data) -> rstd::task::RawWaker {
    ++static_cast<ObserveOnWakerDrop*>(data)->clones;
    return rstd::task::RawWaker::from_raw_parts(data, &observer_vtable);
}
TEST(JoinBorrow, ReplacedWakerCanInspectHandleWithoutDeadlock) {
    auto rt     = RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto job    = rt.spawn(value_task());
    auto state  = ObserveOnWakerDrop { &job };
    auto old    = rstd::task::Waker::from_raw_parts(&state, &observer_vtable);
    auto old_cx = rstd::task::Context { old };
    auto new_cx = rstd::task::Context { rstd::task::Waker::noop() };
    {
        auto borrowed = by_ref(job);
        EXPECT_TRUE(rstd::future::poll(borrowed, old_cx).is_pending());
        EXPECT_TRUE(rstd::future::poll(borrowed, old_cx).is_pending());
        EXPECT_EQ(state.clones, 1);
    }
    state.armed = true;
    {
        auto borrowed = by_ref(job);
        EXPECT_TRUE(rstd::future::poll(borrowed, new_cx).is_pending());
    }
    EXPECT_TRUE(state.observed);
    EXPECT_TRUE(rt.block_on(by_ref(job)).unwrap() == "owned value"_str);
}

struct VoidFuture {
    using Output = void;
    bool* done;
    auto  poll(mut_ref<VoidFuture> self, rstd::task::Context&) -> rstd::task::Poll<void> {
        *self->done = true;
        return rstd::task::Poll<void>::Ready();
    }
};
TEST(JoinBorrow, FutureRefSupportsVoidAndMoves) {
    bool done     = false;
    auto source   = VoidFuture { &done };
    auto borrowed = by_ref(source);
    auto moved    = rstd::move(borrowed);
    auto cx       = rstd::task::Context { rstd::task::Waker::noop() };
    EXPECT_TRUE(rstd::future::poll(moved, cx).is_ready());
    EXPECT_TRUE(done);
}

int main() {
    return rstd::test::run_registered().to_primitive();
}

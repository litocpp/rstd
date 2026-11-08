#include <rstd/test/gtest.hpp>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct FrameProbe {
    int* drops;

    explicit FrameProbe(int& drops): drops(rstd::addressof(drops)) {}
    FrameProbe(const FrameProbe&)            = delete;
    FrameProbe& operator=(const FrameProbe&) = delete;

    FrameProbe(FrameProbe&& other) noexcept: drops(rstd::exchange(other.drops, nullptr)) {}

    ~FrameProbe() {
        if (drops != nullptr) {
            ++*drops;
        }
    }
};

struct PendingFrameFuture {
    using Output = void;

    FrameProbe         probe;
    std::atomic<bool>* polled;

    auto poll(mut_ref<PendingFrameFuture> self, task::Context&) -> task::Poll<void> {
        self->polled->store(true, std::memory_order_release);
        return task::Poll<void>::Pending();
    }
};

auto tracked_leaf(FrameProbe probe) -> async::coro<int> {
    (void)probe;
    co_await async::yield_now();
    co_return 37;
}

auto tracked_parent(FrameProbe probe) -> async::coro<int> {
    co_return co_await tracked_leaf(rstd::move(probe));
}

using FrameTracker = rstd::alloc::SharedTrackingAllocator<>;

[[clang::noinline]]
auto allocated_frame() -> async::coro<int> {
    co_await async::yield_now();
    co_return 41;
}

[[clang::noinline]]
auto frame_with_child() -> async::coro<int> {
    auto child = allocated_frame();
    co_return co_await rstd::move(child);
}

[[clang::noinline]]
auto return_unstarted_frame() -> async::coro<async::coro<int>> {
    co_return allocated_frame();
}

[[clang::noinline]]
auto transfer_frame(async::RuntimeHandle target, FrameTracker& origin, FrameTracker& destination)
    -> async::coro<bool> {
    auto before_origin       = origin.snapshot().allocations;
    auto before_destination  = destination.snapshot().allocations;
    auto child               = frame_with_child();
    bool allocated_by_origin = origin.snapshot().allocations > before_origin &&
                               destination.snapshot().allocations == before_destination;
    auto joined              = target.spawn(rstd::move(child));
    auto result              = co_await rstd::move(joined);
    co_return allocated_by_origin&& result.is_ok() && result.unwrap_unchecked() == 41 &&
        destination.snapshot().allocations > before_destination;
}

[[clang::noinline]]
auto transfer_to_default(async::RuntimeHandle target, FrameTracker& origin) -> async::coro<bool> {
    auto child  = frame_with_child();
    auto count  = origin.snapshot().allocations;
    auto joined = target.spawn(rstd::move(child));
    auto result = co_await rstd::move(joined);
    co_return result.is_ok() && result.unwrap_unchecked() == 41 &&
        origin.snapshot().allocations == count;
}

struct FactoryLifetime {
    std::atomic<int>  calls { 0 };
    std::atomic<int>  frame_drops { 0 };
    std::atomic<int>  factory_drops { 0 };
    std::atomic<bool> dropped_early { false };
};

struct FrameLifetime {
    FactoryLifetime* state;

    ~FrameLifetime() { state->frame_drops.fetch_add(1, std::memory_order_relaxed); }
};

struct CoroutineFactory {
    FactoryLifetime* state;

    explicit CoroutineFactory(FactoryLifetime& state): state(&state) {}
    CoroutineFactory(const CoroutineFactory&) = delete;
    CoroutineFactory(CoroutineFactory&& other) noexcept
        : state(rstd::exchange(other.state, nullptr)) {}

    ~CoroutineFactory() {
        if (state != nullptr) {
            if (state->frame_drops.load(std::memory_order_acquire) == 0)
                state->dropped_early.store(true, std::memory_order_release);
            state->factory_drops.fetch_add(1, std::memory_order_relaxed);
        }
    }

    auto operator()() -> async::coro<int> {
        state->calls.fetch_add(1, std::memory_order_relaxed);
        FrameLifetime lifetime { state };
        co_await async::yield_now();
        co_return state->calls.load(std::memory_order_relaxed) + 40;
    }
};

struct FactoryFrameToken {
    FactoryLifetime* state;

    explicit FactoryFrameToken(FactoryLifetime& state): state(&state) {}
    FactoryFrameToken(const FactoryFrameToken&) = delete;
    FactoryFrameToken(FactoryFrameToken&& other) noexcept
        : state(rstd::exchange(other.state, nullptr)) {}
    ~FactoryFrameToken() {
        if (state != nullptr) state->frame_drops.fetch_add(1, std::memory_order_relaxed);
    }
};

[[clang::noinline]]
auto unstarted_factory_frame(FactoryFrameToken token) -> async::coro<void> {
    (void)token;
    co_return;
}

struct UnstartedCoroutineFactory {
    FactoryLifetime* state;

    explicit UnstartedCoroutineFactory(FactoryLifetime& state): state(&state) {}
    UnstartedCoroutineFactory(const UnstartedCoroutineFactory&) = delete;
    UnstartedCoroutineFactory(UnstartedCoroutineFactory&& other) noexcept
        : state(rstd::exchange(other.state, nullptr)) {}
    ~UnstartedCoroutineFactory() {
        if (state != nullptr) {
            if (state->frame_drops.load(std::memory_order_acquire) == 0)
                state->dropped_early.store(true, std::memory_order_release);
            state->factory_drops.fetch_add(1, std::memory_order_relaxed);
        }
    }
    auto operator()() -> async::coro<void> {
        state->calls.fetch_add(1, std::memory_order_relaxed);
        return unstarted_factory_frame(FactoryFrameToken { *state });
    }
};

struct DelayedCoroutineFactory {
    FactoryLifetime*   state;
    std::atomic<bool>* entered;
    std::atomic<bool>* resume;

    DelayedCoroutineFactory(FactoryLifetime&   state,
                            std::atomic<bool>& entered,
                            std::atomic<bool>& resume)
        : state(&state), entered(&entered), resume(&resume) {}
    DelayedCoroutineFactory(const DelayedCoroutineFactory&) = delete;
    DelayedCoroutineFactory(DelayedCoroutineFactory&& other) noexcept
        : state(rstd::exchange(other.state, nullptr)),
          entered(other.entered),
          resume(other.resume) {}
    ~DelayedCoroutineFactory() {
        if (state != nullptr) {
            if (state->frame_drops.load(std::memory_order_acquire) == 0)
                state->dropped_early.store(true, std::memory_order_release);
            state->factory_drops.fetch_add(1, std::memory_order_relaxed);
        }
    }
    auto operator()() -> async::coro<void> {
        state->calls.fetch_add(1, std::memory_order_relaxed);
        entered->store(true, std::memory_order_release);
        while (! resume->load(std::memory_order_acquire)) hint::spin_loop();
        return unstarted_factory_frame(FactoryFrameToken { *state });
    }
};

struct FactoryPendingFuture {
    using Output = void;
    std::atomic<bool>* polled;

    auto poll(mut_ref<FactoryPendingFuture> self, task::Context&) -> task::Poll<void> {
        self->polled->store(true, std::memory_order_release);
        return task::Poll<void>::Pending();
    }
};

struct PendingCoroutineFactory {
    FactoryLifetime*   state;
    std::atomic<bool>* polled;

    PendingCoroutineFactory(FactoryLifetime& state, std::atomic<bool>& polled)
        : state(&state), polled(&polled) {}
    PendingCoroutineFactory(const PendingCoroutineFactory&) = delete;
    PendingCoroutineFactory(PendingCoroutineFactory&& other) noexcept
        : state(rstd::exchange(other.state, nullptr)), polled(other.polled) {}
    ~PendingCoroutineFactory() {
        if (state != nullptr) {
            if (state->frame_drops.load(std::memory_order_acquire) == 0)
                state->dropped_early.store(true, std::memory_order_release);
            state->factory_drops.fetch_add(1, std::memory_order_relaxed);
        }
    }
    auto operator()() -> async::coro<void> {
        state->calls.fetch_add(1, std::memory_order_relaxed);
        FrameLifetime lifetime { state };
        co_await FactoryPendingFuture { polled };
        co_return;
    }
};

[[clang::noinline]]
auto free_spawn_factories(FactoryLifetime& first, FactoryLifetime& second, FrameTracker& tracker)
    -> async::coro<bool> {
    auto                  before    = tracker.snapshot().allocations;
    auto                  a         = async::spawn_with(CoroutineFactory { first });
    auto                  b         = async::spawn_local_with(CoroutineFactory { second });
    bool                  allocated = tracker.snapshot().allocations >= before + u64(2);
    auto                  a_result  = co_await rstd::move(a);
    auto                  b_result  = co_await rstd::move(b);
    co_return allocated&& a_result.is_ok() && b_result.is_ok() &&
        a_result.unwrap_unchecked() == 41 && b_result.unwrap_unchecked() == 41;
}

TEST(RstdAsyncFrame, FactoryUsesConfiguredAllocatorAcrossRepeatedBuilds) {
    auto tracker   = FrameTracker::make();
    auto allocator = rstd::alloc::SharedAllocator::make(tracker.clone());
    auto builder   = async::RuntimeBuilder::current_thread();
    builder.frame_allocator(allocator.clone());
    auto first  = builder.build().unwrap();
    auto second = builder.build().unwrap();

    auto a = first.handle().spawn_with([] {
        return allocated_frame();
    });
    auto b = second.spawn_local_with([] {
        return allocated_frame();
    });
    EXPECT_GE(tracker.snapshot().allocations, u64(2));
    EXPECT_GT(tracker.snapshot().live_bytes, u64());
    auto a_result = first.block_on_with([task = rstd::move(a)]() mutable {
        return rstd::move(task);
    });
    auto b_result = second.block_on(rstd::move(b));
    ASSERT_TRUE(a_result.is_ok());
    ASSERT_TRUE(b_result.is_ok());
    EXPECT_EQ(a_result.unwrap_unchecked(), 41);
    EXPECT_EQ(b_result.unwrap_unchecked(), 41);
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(RstdAsyncFrame, PrecreatedRootKeepsItsOriginalAllocator) {
    auto tracker   = FrameTracker::make();
    auto allocator = rstd::alloc::SharedAllocator::make(tracker.clone());
    auto builder   = async::RuntimeBuilder::current_thread();
    builder.frame_allocator(allocator.clone());
    auto runtime = builder.build().unwrap();

    auto root   = allocated_frame();
    auto before = tracker.snapshot().allocations;
    auto task   = runtime.spawn(rstd::move(root));
    EXPECT_EQ(tracker.snapshot().allocations, before);
    auto result = runtime.block_on(rstd::move(task));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.unwrap_unchecked(), 41);
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(RstdAsyncFrame, AcrossRuntimesRootAndChildrenUseTheirOwners) {
    auto tracker_a   = FrameTracker::make();
    auto tracker_b   = FrameTracker::make();
    auto allocator_a = rstd::alloc::SharedAllocator::make(tracker_a.clone());
    auto allocator_b = rstd::alloc::SharedAllocator::make(tracker_b.clone());
    auto builder_a   = async::RuntimeBuilder::multi_thread();
    auto builder_b   = async::RuntimeBuilder::multi_thread();
    builder_a.worker_threads(usize(1)).frame_allocator(allocator_a.clone());
    builder_b.worker_threads(usize(1)).frame_allocator(allocator_b.clone());
    auto a = builder_a.build().unwrap();
    auto b = builder_b.build().unwrap();

    auto joined = a.spawn_with([target = b.handle(), &tracker_a, &tracker_b]() mutable {
        return transfer_frame(rstd::move(target), tracker_a, tracker_b);
    });
    auto result = a.block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.unwrap_unchecked());
    EXPECT_EQ(tracker_a.snapshot().live_bytes, u64());
    EXPECT_EQ(tracker_b.snapshot().live_bytes, u64());
}

TEST(RstdAsyncFrame, DefaultRuntimeDoesNotInheritAnotherRuntimeAllocator) {
    auto tracker   = FrameTracker::make();
    auto allocator = rstd::alloc::SharedAllocator::make(tracker.clone());
    auto builder_a = async::RuntimeBuilder::multi_thread();
    builder_a.worker_threads(usize(1)).frame_allocator(allocator.clone());
    auto a = builder_a.build().unwrap();
    auto b = async::RuntimeBuilder::multi_thread().worker_threads(usize(1)).build().unwrap();

    auto joined = a.spawn_with([target = b.handle(), &tracker]() mutable {
        return transfer_to_default(rstd::move(target), tracker);
    });
    for (int i = 0; i < 2000 && ! joined.is_finished(); ++i)
        thread::sleep(time::Duration::from_millis(u64(1)));
    ASSERT_TRUE(joined.is_finished());
    auto result = async::block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(result.unwrap_unchecked());
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(RstdAsyncFrame, CoroutineFactoryOutlivesItsFrame) {
    FactoryLifetime state;
    {
        auto tracker   = FrameTracker::make();
        auto allocator = rstd::alloc::SharedAllocator::make(tracker.clone());
        auto builder   = async::RuntimeBuilder::current_thread();
        builder.frame_allocator(allocator.clone());
        auto runtime = builder.build().unwrap();
        auto joined  = runtime.spawn_with(CoroutineFactory { state });
        auto result  = runtime.block_on(rstd::move(joined));
        ASSERT_TRUE(result.is_ok());
        EXPECT_EQ(result.unwrap_unchecked(), 41);
    }
    EXPECT_EQ(state.calls.load(std::memory_order_acquire), 1);
    EXPECT_EQ(state.frame_drops.load(std::memory_order_acquire), 1);
    EXPECT_EQ(state.factory_drops.load(std::memory_order_acquire), 1);
    EXPECT_FALSE(state.dropped_early.load(std::memory_order_acquire));
}

TEST(RstdAsyncFrame, ShutdownDestroysUnstartedFactoryFrameFirst) {
    FactoryLifetime state;
    auto            tracker = FrameTracker::make();
    auto            joined  = [&] {
        auto builder = async::RuntimeBuilder::current_thread();
        builder.frame_allocator(rstd::alloc::SharedAllocator::make(tracker.clone()));
        auto runtime = builder.build().unwrap();
        return runtime.spawn_with(UnstartedCoroutineFactory { state });
    }();
    EXPECT_EQ(state.calls.load(std::memory_order_acquire), 1);
    EXPECT_EQ(state.frame_drops.load(std::memory_order_acquire), 1);
    EXPECT_EQ(state.factory_drops.load(std::memory_order_acquire), 1);
    EXPECT_FALSE(state.dropped_early.load(std::memory_order_acquire));
    EXPECT_TRUE(joined.is_finished());
    auto result = async::block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err().is_aborted());
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(RstdAsyncFrame, ShutdownDuringFactoryRejectsAndDestroysFrameFirst) {
    FactoryLifetime   state;
    std::atomic<bool> entered { false };
    std::atomic<bool> resume { false };
    auto              tracker = FrameTracker::make();
    auto              builder = async::RuntimeBuilder::multi_thread();
    builder.worker_threads(usize(1)).frame_allocator(
        rstd::alloc::SharedAllocator::make(tracker.clone()));
    auto runtime = Some(builder.build().unwrap());
    auto producer =
        thread::spawn([handle = runtime->handle(), &state, &entered, &resume]() mutable {
            return handle.spawn_with(DelayedCoroutineFactory { state, entered, resume });
        }).unwrap();
    for (int i = 0; i < 2000 && ! entered.load(std::memory_order_acquire); ++i)
        thread::sleep(time::Duration::from_millis(u64(1)));
    auto factory_entered = entered.load(std::memory_order_acquire);
    if (! factory_entered) {
        resume.store(true, std::memory_order_release);
        auto joined = rstd::move(producer).join().unwrap();
        runtime     = None();
        EXPECT_TRUE(factory_entered);
        return;
    }

    runtime = None();
    resume.store(true, std::memory_order_release);
    auto joined = rstd::move(producer).join().unwrap();
    EXPECT_TRUE(joined.is_finished());
    auto result = async::block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err().is_aborted());
    EXPECT_EQ(state.calls.load(std::memory_order_acquire), 1);
    EXPECT_EQ(state.frame_drops.load(std::memory_order_acquire), 1);
    EXPECT_EQ(state.factory_drops.load(std::memory_order_acquire), 1);
    EXPECT_FALSE(state.dropped_early.load(std::memory_order_acquire));
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(RstdAsyncFrame, ConfiguredAllocatorFailureUsesAllocationFailurePolicy) {
    auto budget  = rstd::alloc::BudgetAllocator<>::try_make(usize()).unwrap();
    auto builder = async::RuntimeBuilder::current_thread();
    builder.frame_allocator(rstd::alloc::SharedAllocator::make(rstd::move(budget)));
    auto runtime = builder.build().unwrap();
    EXPECT_DEATH((void)runtime.spawn_with([] {
        return allocated_frame();
    }),
                 "memory allocation failed");
}

TEST(RstdAsyncFrame, AbortDestroysPendingFactoryFrameFirst) {
    FactoryLifetime   state;
    std::atomic<bool> polled { false };
    auto              tracker = FrameTracker::make();
    auto              builder = async::RuntimeBuilder::multi_thread();
    builder.worker_threads(usize(1)).frame_allocator(
        rstd::alloc::SharedAllocator::make(tracker.clone()));
    auto runtime = builder.build().unwrap();
    auto joined  = runtime.spawn_with(PendingCoroutineFactory { state, polled });
    for (int i = 0; i < 2000 && ! polled.load(std::memory_order_acquire); ++i)
        thread::sleep(time::Duration::from_millis(u64(1)));
    ASSERT_TRUE(polled.load(std::memory_order_acquire));
    joined.abort();
    auto result = runtime.block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err().is_aborted());
    EXPECT_EQ(state.calls.load(std::memory_order_acquire), 1);
    EXPECT_EQ(state.frame_drops.load(std::memory_order_acquire), 1);
    EXPECT_EQ(state.factory_drops.load(std::memory_order_acquire), 1);
    EXPECT_FALSE(state.dropped_early.load(std::memory_order_acquire));
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(RstdAsyncFrame, FreeSpawnFactoriesKeepCapturesAndUseCurrentRuntime) {
    FactoryLifetime first;
    FactoryLifetime second;
    auto            tracker   = FrameTracker::make();
    auto            allocator = rstd::alloc::SharedAllocator::make(tracker.clone());
    {
        auto builder = async::RuntimeBuilder::current_thread();
        builder.frame_allocator(allocator.clone());
        auto runtime = builder.build().unwrap();
        EXPECT_TRUE(runtime.block_on_with([&] {
            return free_spawn_factories(first, second, tracker);
        }));
    }
    EXPECT_EQ(first.calls.load(std::memory_order_acquire), 1);
    EXPECT_EQ(second.calls.load(std::memory_order_acquire), 1);
    EXPECT_EQ(first.frame_drops.load(std::memory_order_acquire), 1);
    EXPECT_EQ(second.frame_drops.load(std::memory_order_acquire), 1);
    EXPECT_EQ(first.factory_drops.load(std::memory_order_acquire), 1);
    EXPECT_EQ(second.factory_drops.load(std::memory_order_acquire), 1);
    EXPECT_FALSE(first.dropped_early.load(std::memory_order_acquire));
    EXPECT_FALSE(second.dropped_early.load(std::memory_order_acquire));
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(RstdAsyncFrame, BorrowedFrameAllocatorSupportsCrossThreadRelease) {
    auto tracker   = FrameTracker::make();
    auto allocator = rstd::alloc::SharedAllocator::make(tracker.clone());
    auto builder   = async::RuntimeBuilder::current_thread();
    builder.frame_allocator(rstd::move(allocator));
    auto runtime = builder.build().unwrap();
    auto frame   = runtime.block_on_with([] {
        return return_unstarted_frame();
    });

    EXPECT_GT(tracker.snapshot().live_bytes, u64());
    auto worker = thread::spawn([frame = rstd::move(frame)]() mutable {
                      auto local = rstd::move(frame);
                  }).unwrap();
    rstd::move(worker).join().unwrap();
    EXPECT_EQ(tracker.snapshot().live_bytes, u64());
}

TEST(RstdAsyncFrame, MoveTransfersSoleFrameOwnership) {
    int  drops    = 0;
    auto original = tracked_leaf(FrameProbe { drops });
    EXPECT_EQ(drops, 0);

    auto moved = rstd::move(original);
    EXPECT_EQ(async::block_on(rstd::move(moved)), 37);
    EXPECT_EQ(drops, 1);
}

TEST(RstdAsyncFrame, NestedChildFrameDestroysOwnedStateOnce) {
    int drops = 0;

    EXPECT_EQ(async::block_on(tracked_parent(FrameProbe { drops })), 37);
    EXPECT_EQ(drops, 1);
}

TEST(RstdAsyncFrame, ShutdownDestroysPendingFutureFrameOnce) {
    int  drops  = 0;
    auto polled = std::atomic<bool> { false };
    auto joined = [&] {
        auto runtime =
            async::RuntimeBuilder::multi_thread().worker_threads(usize(1)).build().unwrap();
        auto handle = runtime.spawn(PendingFrameFuture { FrameProbe { drops }, &polled });
        while (! polled.load(std::memory_order_acquire)) {
            hint::spin_loop();
        }
        return handle;
    }();

    EXPECT_EQ(drops, 1);
    EXPECT_TRUE(joined.is_finished());
    auto result = async::block_on(rstd::move(joined));
    ASSERT_TRUE(result.is_err());
    EXPECT_TRUE(rstd::move(result).unwrap_err().is_aborted());
    EXPECT_EQ(drops, 1);
}

} // namespace
